#include "common.h"

static PyObject *BencodeDecodeError;

static inline PyObject *decodeError(const char *fmt, ...);
static PyObject *decode_any(const char *buf, Py_ssize_t *index, Py_ssize_t size);
static PyObject *decode_int(const char *buf, Py_ssize_t *index, Py_ssize_t size);
static PyObject *decode_bytes(const char *buf, Py_ssize_t *index, Py_ssize_t size);
static PyObject *decode_dict(const char *buf, Py_ssize_t *index, Py_ssize_t size);
static PyObject *decode_list(const char *buf, Py_ssize_t *index, Py_ssize_t size);

static PyObject *bdecode(PyObject *self, PyObject *b) {
  if (!PyBytes_Check(b)) {
    PyErr_SetString(PyExc_TypeError, "can only decode bytes");
    return NULL;
  }

  Py_ssize_t size = PyBytes_Size(b);
  const char *buf = PyBytes_AsString(b);

  Py_ssize_t index = 0;

  PyObject *r = decode_any(buf, &index, size);

  if (index != size) {
    return decodeError("invalid bencode data, index %d", index);
  }

  // return Py_BuildValue("");
  return r;
  // return Py_None;
}

static inline PyObject *decodeError(const char *fmt, ...) {
  PyErr_SetObject(BencodeDecodeError, PyUnicode_FromFormat(fmt));
  return NULL;
}

static PyObject *decode_int(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  Py_ssize_t index_e = 0;

  for (Py_ssize_t i = *index; i < size; i++) {
    if (buf[i] == 'e') {
      index_e = i;
      break;
    }
  }

  if (index_e == 0) {
    return decodeError("invalid int, missing 'e': %d", *index);
  }

  long long sign = 1;
  *index = *index + 1;
  if (buf[*index] == '-') {
    if (buf[*index + 1] == '0') {
      return decodeError("invalid int, '-0' found at %d", *index);
    }

    sign = -1;
    *index = *index + 1;
  } else if (buf[*index] == '0') {
    if (*index + 1 != index_e) {
      return decodeError("invalid int, non-zero int should not start with '0'. found at %d", *index);
    }
  }

  long long val = 0;
  for (Py_ssize_t i = *index; i < index_e; i++) {
    if (buf[i] > '9' || buf[i] < '0') {
      return decodeError("invalid int, '%c' found at %d", buf[i], i);
    }
    val = val * 10 + (buf[i] - '0');
  }

  val = val * sign;

  *index = index_e + 1;
  return PyLong_FromLongLong(val);
}

// // there is no bytes/str in bencode, they only have 1 type for both of them.
static PyObject *decode_bytes(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  Py_ssize_t index_sep = 0;
  for (Py_ssize_t i = *index; i < size; i++) {
    if (buf[i] == ':') {
      index_sep = i;
      break;
    }
  }

  if (index_sep == 0) {
    return decodeError("invalid string, missing length: index %d", *index);
  }

  if (buf[*index] == '0' && *index + 1 != index_sep) {
    return decodeError("invalid bytes length, found at %d", *index);
  }

  Py_ssize_t len = 0;
  for (Py_ssize_t i = *index; i < index_sep; i++) {
    if (buf[i] < '0' || buf[i] > '9') {
      return decodeError("invalid bytes length, found '%c' at %d", buf[i], i);
    }
    len = len * 10 + (buf[i] - '0');
  }

  if (index_sep + len >= size) {
    return decodeError("bytes length overflow, index %d", *index);
  }

  *index = index_sep + len + 1;

  return PyBytes_FromStringAndSize(&buf[index_sep + 1], len);
}

static PyObject *decode_list(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  *index = *index + 1;

  PyObject *l = PyList_New(0);

  while (buf[*index] != 'e') {
    PyObject *obj = decode_any(buf, index, size);
    if (obj == NULL) {
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

static PyObject *decode_dict(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  *index = *index + 1;
  PyObject *d = PyDict_New();
  const char *lastKey = NULL;
  size_t lastKeyLen = 0;
  const char *currentKey;
  size_t currentKeyLen;
  while (buf[*index] != 'e') {
    PyObject *key = decode_bytes(buf, index, size);
    if (key == NULL) {
      return NULL;
    }

    PyObject *obj = decode_any(buf, index, size);
    if (obj == NULL) {
      return NULL;
    }
    currentKeyLen = PyBytes_Size(key);
    currentKey = PyBytes_AsString(key);
    // skip first key
    if (lastKey != NULL) {
      if (strCompare(currentKey, currentKeyLen, lastKey, lastKeyLen) < 0) {
        return decodeError("invalid dict, key not sorted. index %d", *index);
      }
    }
    lastKey = currentKey;
    lastKeyLen = currentKeyLen;
    PyDict_SetItem(d, key, obj);
    Py_DecRef(key);
    Py_DecRef(obj);
  }
  *index = *index + 1;
  return d;
}

static PyObject *decode_any(const char *buf, Py_ssize_t *index, Py_ssize_t size) {
  // int
  if (buf[*index] == 'i') {
    return decode_int(buf, index, size);
  }
  if (buf[*index] >= '0' && buf[*index] <= '9') {
    return decode_bytes(buf, index, size);
  }
  // // list
  if (buf[*index] == 'l') {
    return decode_list(buf, index, size);
  }

  if (buf[*index] == 'd') {
    return decode_dict(buf, index, size);
  }

  return decodeError("invalid bencode prefix '%c', index %d", buf[*index], *index);
}
