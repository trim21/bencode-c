#include "common.h"
#include "overflow.h"
#include "str.h"

static HPy bdecode(HPy mod, HPy obj);

// module level variable
PyObject *BencodeDecodeError;
PyDoc_STRVAR(__bdecode_doc__, "bdecode(b: bytes, /) -> Any\n"
                              "--\n\n"
                              "decode bytes to python object");
PyMethodDef decodeImpl = {
    .ml_name = "bdecode",
    .ml_meth = bdecode,
    .ml_flags = METH_O,
    .ml_doc = __bdecode_doc__,
};
// module level variable

static PyObject *decodeAny(const char *buf, Py_ssize_t *index, Py_ssize_t size);

static inline PyObject *formatError(HPy err, const char *format, ...) {
  va_list args;

  va_start(args, format);

  struct str s;
  if (va_str_printf(&s, format, args)) {
    va_end(args);
    //    PyErr_SetString(PyExc_MemoryError, "failed to format string");
    return NULL;
  }
  va_end(args);

  HPy o = PyUnicode_FromStringAndSize(s.str, s.size);
  PyErr_SetObject(err, o);
  Py_DecRef(o);
  free(s.str);
  return NULL;
}

#define decodingError(format, ...)                                                                 \
  do {                                                                                             \
    formatError(BencodeDecodeError, format, __VA_ARGS__);                                          \
  } while (0)

static PyObject *decodeInt(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  Py_ssize_t index_e = 0;
  for (Py_ssize_t i = *index + 1; i < size; i++) {
    if (buf[i] == 'e') {
      index_e = i;
      break;
    }
  }

  if (index_e == 0) {
    decodingError("invalid int, missing 'e': %zd", *index);
    return NULL;
  }

  // malformed 'ie'
  if (*index + 1 == index_e) {
    decodingError("invalid int, found 'ie': %zd", index_e);
    return NULL;
  }

  int sign = 1;

  // i1234e
  // i-1234e
  //  ^ index
  *index = *index + 1;

  Py_ssize_t numStart = *index;

  if (buf[*index] == '-') {
    if (buf[*index + 1] == '0') {
      decodingError("invalid int, '-0' found at %zd", *index);
      return NULL;
    }

    sign = -1;
    numStart++;
  } else if (buf[*index] == '0') {
    if (*index + 1 != index_e) {
      decodingError("invalid int, non-zero int should not start with '0'. found at %zd", *index);
      return NULL;
    }
  }

  // i1234e
  //  ^ numStart
  // i-1234e
  //   ^ numStart
  //  ^ index

  for (Py_ssize_t i = numStart; i < index_e; i++) {
    char c = buf[i];
    if (c < '0' || c > '9') {
      decodingError("invalid int, '%c' found at %zd", c, i);
      return NULL;
    }
  }

  if (sign > 0) {
    unsigned long long val = 0;
    for (Py_ssize_t i = *index; i < index_e; i++) {
      char c = buf[i] - '0';
      // val = val * 10 + (buf[i] - '0')
      // but with overflow check

      int of = _u128_mul_overflow(val, 10, &val);
      of = of || _u128_add_overflow(val, c, &val);

      if (of) {
        goto __OverFlow;
      }
    }

    *index = index_e + 1;

    return PyLong_FromUnsignedLongLong(val);
  } else {
    long long val = 0;
    int of;
    for (Py_ssize_t i = *index + 1; i < index_e; i++) {
      char c = buf[i] - '0';

      of = _i128_mul_overflow(val, 10, &val);
      of = of || _i128_add_overflow(val, c, &val);

      if (of) {
        goto __OverFlow;
      }
    }

    if (_i128_mul_overflow(val, sign, &val)) {
      goto __OverFlow;
    }

    *index = index_e + 1;
    return PyLong_FromLongLong(val);
  }

  // i1234e
  //  ^ index
  // i-1234e
  //  ^ index

// bencode int overflow u128 or i128, build a PyLong object from str directly.
__OverFlow:
  const size_t n = index_e - *index + 1;
  char *s = (char *)malloc(n);
  if (s == NULL) {
    PyErr_SetString(PyExc_MemoryError, "failed to memory");

    return NULL;
  }

  strncpy(s, &buf[*index], n);

  *index = index_e + 1;

  s[n - 1] = 0;

  HPy i = PyLong_FromString(s, NULL, 10);

  free(s);

  return i;
}

// // there is no bytes/str in bencode, they only have 1 type for both of them.
static PyObject *decodeBytes(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  Py_ssize_t index_sep = 0;
  for (Py_ssize_t i = *index; i < size; i++) {
    if (buf[i] == ':') {
      index_sep = i;
      break;
    }
  }

  if (index_sep == 0) {
    decodingError("invalid string, missing length: index %zd", *index);
    return NULL;
  }

  if (buf[*index] == '0' && *index + 1 != index_sep) {
    decodingError("invalid bytes length, found at %zd", *index);
    return NULL;
  }

  Py_ssize_t len = 0;
  for (Py_ssize_t i = *index; i < index_sep; i++) {
    if (buf[i] < '0' || buf[i] > '9') {
      decodingError("invalid bytes length, found '%c' at %zd", buf[i], i);
      return NULL;
    }
    len = len * 10 + (buf[i] - '0');
  }

  if (index_sep + len >= size) {
    decodingError("bytes length overflow, index %zd", *index);
    return NULL;
  }

  *index = index_sep + len + 1;

  return PyBytes_FromStringAndSize(&buf[index_sep + 1], len);
}

static PyObject *decodeList(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  *index = *index + 1;

  PyObject *l = PyList_New(0);

  while (buf[*index] != 'e') {
    PyObject *obj = decodeAny(buf, index, size);
    if (obj == NULL) {
      Py_DecRef(l);
      return NULL;
    }

    PyList_Append(l, obj);

    Py_DecRef(obj);
  }

  *index = *index + 1;

  return l;
}

static int strCompare(const char *s1, size_t len1, const char *s2, size_t len2) {
  size_t min_len = (len1 < len2) ? len1 : len2;
  int result = strncmp(s1, s2, min_len);

  if (result != 0) {
    return result;
  }

  // first min_len have same characters.
  if (len1 < len2) {
    return -1;
  } else if (len1 > len2) {
    return 1;
  } else {
    return 0;
  }
}

static void decodeDict(const char *buf, Py_ssize_t *index, Py_ssize_t size, PyObject *d) {
  *index = *index + 1;
  const char *lastKey = NULL;
  Py_ssize_t lastKeyLen = 0;
  const char *currentKey;
  Py_ssize_t currentKeyLen;
  while (buf[*index] != 'e') {
    PyObject *key = decodeBytes(buf, index, size);
    if (key == NULL) {
      return;
    }

    PyObject *obj = decodeAny(buf, index, size);
    if (obj == NULL) {
      Py_DecRef(key);
      return;
    }
    currentKeyLen = PyBytes_Size(key);
    currentKey = PyBytes_AsString(key);
    // skip first key
    if (lastKey != NULL) {
      int keyCmp = strCompare(currentKey, currentKeyLen, lastKey, lastKeyLen);
      if (keyCmp < 0) {
        decodingError("invalid dict, key not sorted. index %zd", *index);
        return;
      }
      if (keyCmp == 0) {
        decodingError("invalid dict, find duplicated keys %.*s. index %zd", currentKeyLen,
                      currentKey, *index);
        return;
      }
    }
    lastKey = currentKey;
    lastKeyLen = currentKeyLen;
    PyDict_SetItem(d, key, obj);
    Py_DecRef(key);
    Py_DecRef(obj);
  }
  *index = *index + 1;
}

static PyObject *decodeAny(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  // int
  if (buf[*index] == 'i') {
    return decodeInt(buf, index, size);
  }

  // bytes
  if (buf[*index] >= '0' && buf[*index] <= '9') {
    return decodeBytes(buf, index, size);
  }

  // list
  if (buf[*index] == 'l') {
    return decodeList(buf, index, size);
  }

  // dict
  if (buf[*index] == 'd') {
    PyObject *dict = PyDict_New();

    decodeDict(buf, index, size, dict);
    if (dict == NULL) {
      Py_DecRef(dict);
      return NULL;
    }

    return dict;
  }

  decodingError("invalid bencode prefix '%c', index %zd", buf[*index], *index);
  return NULL;
}

static PyObject *bdecode(PyObject *self, PyObject *b) {
  if (!PyBytes_Check(b)) {
    PyErr_SetString(PyExc_TypeError, "can only decode bytes");
    return NULL;
  }

  Py_ssize_t size = PyBytes_Size(b);
  const char *buf = PyBytes_AsString(b);

  Py_ssize_t index = 0;
  PyObject *r = decodeAny(buf, &index, size);
  if (r == NULL) {
    // failed to parse
    return NULL;
  }

  if (index != size) {
    Py_XDECREF(r);

    decodingError("invalid bencode data, parse end at index %zd but total bytes length %zd", index,
                  size);
    return NULL;
  }

  return r;
}
