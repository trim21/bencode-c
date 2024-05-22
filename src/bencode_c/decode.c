#include "common.h"

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

static inline PyObject *decodingError(const char *format, ...);

static PyObject *decodeAny(const char *buf, Py_ssize_t *index, Py_ssize_t size);
static PyObject *decodeInt(const char *buf, Py_ssize_t *index, Py_ssize_t size);
static PyObject *decodeBytes(const char *buf, Py_ssize_t *index, Py_ssize_t size);
static void decodeDict(const char *buf, Py_ssize_t *index, Py_ssize_t size, PyObject *dict);
static PyObject *decodeList(const char *buf, Py_ssize_t *index, Py_ssize_t size);

static PyObject *bdecode(PyObject *self, PyObject *b) {
  if (!PyBytes_Check(b)) {
    PyErr_SetString(PyExc_TypeError, "can only decode bytes");
    return NULL;
  }

  Py_ssize_t size = PyBytes_Size(b);
  const char *buf = PyBytes_AsString(b);

  Py_ssize_t index = 0;

  PyObject *r = decodeAny(buf, &index, size);

  if (index != size) {
    return decodingError("invalid bencode data, index %d", index);
  }

  return r;
}

static inline PyObject *decodingError(const char *format, ...) {
  PyErr_SetObject(BencodeDecodeError, PyUnicode_FromFormat(format));
  return NULL;
}

static PyObject *decodeInt(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  Py_ssize_t index_e = 0;

  for (Py_ssize_t i = *index; i < size; i++) {
    if (buf[i] == 'e') {
      index_e = i;
      break;
    }
  }

  if (index_e == 0) {
    return decodingError("invalid int, missing 'e': %d", *index);
  }

  long long sign = 1;
  *index = *index + 1;
  if (buf[*index] == '-') {
    if (buf[*index + 1] == '0') {
      return decodingError("invalid int, '-0' found at %d", *index);
    }

    sign = -1;
    *index = *index + 1;
  } else if (buf[*index] == '0') {
    if (*index + 1 != index_e) {
      return decodingError("invalid int, non-zero int should not start with '0'. found at %d",
                           *index);
    }
  }

  long long val = 0;
  for (Py_ssize_t i = *index; i < index_e; i++) {
    if (buf[i] > '9' || buf[i] < '0') {
      return decodingError("invalid int, '%c' found at %d", buf[i], i);
    }
    val = val * 10 + (buf[i] - '0');
  }

  val = val * sign;

  *index = index_e + 1;
  return PyLong_FromLongLong(val);
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
    return decodingError("invalid string, missing length: index %d", *index);
  }

  if (buf[*index] == '0' && *index + 1 != index_sep) {
    return decodingError("invalid bytes length, found at %d", *index);
  }

  Py_ssize_t len = 0;
  for (Py_ssize_t i = *index; i < index_sep; i++) {
    if (buf[i] < '0' || buf[i] > '9') {
      return decodingError("invalid bytes length, found '%c' at %d", buf[i], i);
    }
    len = len * 10 + (buf[i] - '0');
  }

  if (index_sep + len >= size) {
    return decodingError("bytes length overflow, index %d", *index);
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
  size_t lastKeyLen = 0;
  const char *currentKey;
  size_t currentKeyLen;
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
        decodingError("invalid dict, key not sorted. index %d", *index);
        return;
      }
      if (keyCmp == 0) {
        decodingError("invalid dict, find duplicated keys %.*s. index %d", currentKeyLen,
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
  if (buf[*index] >= '0' && buf[*index] <= '9') {
    return decodeBytes(buf, index, size);
  }
  // // list
  if (buf[*index] == 'l') {
    return decodeList(buf, index, size);
  }

  if (buf[*index] == 'd') {
    PyObject *dict = PyDict_New();

    decodeDict(buf, index, size, dict);
    if (dict == NULL) {
      Py_DecRef(dict);
      return NULL;
    }

    return dict;
  }

  return decodingError("invalid bencode prefix '%c', index %d", buf[*index], *index);
}
