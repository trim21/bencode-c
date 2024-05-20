#include "stdio.h"
#include "string.h"

#include "common.h"

static PyObject *BencodeEncodeError;

#define NON_SUPPORTED_TYPE_MESSAGE                                                                                     \
  "invalid type '%s', "                                                                                                \
  "bencode only support bytes, str, int, list, tuple, dict and bool(encoded as 0/1, decoded as int)"

#define defaultBufferSize 4096

#define HPyLong_Check(obj) Py_TypeCheck(obj, PyLong_Type)
#define returnIfError(o)                                                                                               \
  if (o) {                                                                                                             \
    return o;                                                                                                          \
  }

static inline void runtimeError(const char *data) { PyErr_SetString(PyExc_RuntimeError, data); }

static inline void typeError(const char *data) { PyErr_SetString(PyExc_TypeError, data); }

static inline HPy bencodeError(const char *data) {
  PyErr_SetString(BencodeEncodeError, data);
  return NULL;
}

struct buffer {
  char *buf;
  size_t len;
  size_t cap;
};

static struct buffer *newBuffer() {
  struct buffer *v = (struct buffer *)malloc(sizeof(struct buffer));

  v->buf = (char *)malloc(defaultBufferSize);
  v->len = 0;
  v->cap = defaultBufferSize;

  return v;
}

static int bufferWrite(struct buffer *buf, const char *data, HPy_ssize_t size) {
  void *tmp;

  if (size + buf->len >= buf->cap) {
    tmp = realloc(buf->buf, buf->cap * 2 + size);
    if (tmp == NULL) {
      runtimeError("realloc failed");
      return 1;
    }
    buf->cap = buf->cap * 2 + size;
    buf->buf = (char *)tmp;
  }

  memcpy(buf->buf + buf->len, data, size);

  buf->len = buf->len + size;

  return 0;
}

static int bufferWriteLong(struct buffer *buf, long long val) {

  int j = snprintf(NULL, 0, "%lld", val) + 1;
  char *s = (char *)malloc(sizeof(char) * j);
  snprintf(s, j, "%lld", val);
  int r = bufferWrite(buf, s, j - 1);
  free(s);
  return r;
}

static void freeBuffer(struct buffer *buf) {
  free(buf->buf);
  free(buf);
}

// obj must be a python dict object.
// TODO: use c native struct and sorting
static int buildDictKeyList(HPy obj, HPy *list, HPy_ssize_t *count) {
  HPy keys = PyDict_Keys(obj);
  if (keys == NULL) {
    runtimeError("failed to get dict keys");
    return 1;
  }

  *count = PyObject_Length(keys);

  *list = PyList_New(0);

  for (HPy_ssize_t i = 0; i < *count; i++) {
    HPy key = PySequence_GetItem(keys, i);
    HPy value = PyDict_GetItem(obj, key);
    HPy keyAsBytes = key;
    if (PyUnicode_Check(key)) {
      keyAsBytes = PyUnicode_AsUTF8String(key);
    } else if (!PyBytes_Check(key)) {
      bencodeError("dict key must be str or bytes");
      Py_DecRef(key);
      return 1;
    }

    HPy tu = PyTuple_Pack(2, keyAsBytes, value);
    if (tu == NULL) {
      runtimeError("can not pack key value pair");
      return 1;
    }

    PyList_Append(*list, tu);
  }

  if (PyObject_CallMethod(*list, "sort", NULL) == NULL) {
    return 1;
  }

  // check duplicated keys
  const char *currentKey = NULL;
  size_t currentKeylen = 0;
  const char *lastKey = NULL;
  size_t lastKeylen = 0;

  for (HPy_ssize_t i = 0; i < *count; i++) {
    HPy keyValue = PyList_GetItem(*list, i);
    HPy key = PyTuple_GetItem(keyValue, 0);
    currentKeylen = PyBytes_Size(key);
    currentKey = PyBytes_AsString(key);
    if (lastKey != NULL) {
      if (lastKeylen == currentKeylen) {
        if (strncmp(lastKey, currentKey, lastKeylen) == 0) {
          bencodeError("find duplicated keys with str and bytes in dict");
          return 1;
        }
      }
    }

    lastKey = currentKey;
    lastKeylen = currentKeylen;
  }

  return 0;
}
static int encodeBytes(struct buffer *buf, HPy obj) {
  HPy_ssize_t size = PyBytes_Size(obj);
  const char *data = PyBytes_AsString(obj);

  returnIfError(bufferWriteLong(buf, size));
  returnIfError(bufferWrite(buf, ":", 1));
  return bufferWrite(buf, data, size);
}

static int encodeAny(struct buffer *buf, HPy obj) {
  if (Py_True == obj) {
    return bufferWrite(buf, "i1e", 3);
  } else if (Py_False == obj) {
    return bufferWrite(buf, "i0e", 3);
  } else if (PyBytes_Check(obj)) {
    return encodeBytes(buf, obj);
  } else if (PyUnicode_Check(obj)) {

#if PY_VERSION_HEX >= 0x03100000
    HPy_ssize_t size;
    const char *data = PyUnicode_AsUTF8AndSize(obj, &size);
#else
    HPy b = PyUnicode_AsUTF8String(obj);
    HPy_ssize_t size = PyBytes_Size(b);
    const char *data = PyBytes_AsString(b);
#endif

    returnIfError(bufferWriteLong(buf, size));
    returnIfError(bufferWrite(buf, ":", 1));
    return bufferWrite(buf, data, size);
  } else if (PyLong_Check(obj)) {
    long long val = PyLong_AsLongLong(obj);

    // python int may overflow c long long
    if (PyErr_Occurred()) {
      return 1;
    }

    returnIfError(bufferWrite(buf, "i", 1));
    returnIfError(bufferWriteLong(buf, val));
    return bufferWrite(buf, "e", 1);
  } else if (PyList_Check(obj)) {
    HPy_ssize_t len = PyList_Size(obj);
    returnIfError(bufferWrite(buf, "l", 1));

    for (HPy_ssize_t i = 0; i < len; i++) {
      HPy o = PyList_GetItem(obj, i);
      returnIfError(encodeAny(buf, o));
    }
    return bufferWrite(buf, "e", 1);
  } else if (PyTuple_Check(obj)) {
    HPy_ssize_t len = PyTuple_Size(obj);
    returnIfError(bufferWrite(buf, "l", 1));

    for (HPy_ssize_t i = 0; i < len; i++) {
      HPy o = PyTuple_GetItem(obj, i);
      returnIfError(encodeAny(buf, o));
    }
    return bufferWrite(buf, "e", 1);

  } else if (PyDict_Check(obj)) {
    returnIfError(bufferWrite(buf, "d", 1));
    HPy list = NULL;
    HPy_ssize_t count = 0;
    if (buildDictKeyList(obj, &list, &count)) {
      return 1;
    }

    for (HPy_ssize_t i = 0; i < count; i++) {
      HPy keyValue = PyList_GetItem(list, i); // tuple[bytes, Any]
      if (keyValue == NULL) {
        runtimeError("failed to get key/value tuple from list");
        return 1;
      }
      HPy key = PyTuple_GetItem(keyValue, 0);
      HPy value = PyTuple_GetItem(keyValue, 1);
      if (key == NULL) {
        runtimeError("can't get key from key,value tuple");
        return 1;
      }
      if (value == NULL) {
        runtimeError("can't get value");
        return 1;
      }
      if (encodeBytes(buf, key)) {
        Py_DecRef(list);
        return 1;
      }
      if (encodeAny(buf, value)) {
        Py_DecRef(list);
        return 1;
      }

      Py_DecRef(key);
      Py_DecRef(value);
    }

    Py_DecRef(list);
    return bufferWrite(buf, "e", 1);
  }

  HPy typ = PyObject_Type(obj);

  PyErr_SetObject(BencodeEncodeError, PyUnicode_Format(PyUnicode_FromString(NON_SUPPORTED_TYPE_MESSAGE), typ));

  return 1;
}

static HPy bencode(HPy self, HPy obj) {
  // self is the module object

  struct buffer *buf = newBuffer();
  Py_IncRef(obj);

  if (encodeAny(buf, obj)) {
    freeBuffer(buf);
    Py_DecRef(obj);
    // error when encoding
    return NULL;
  }

  HPy res = PyBytes_FromStringAndSize(buf->buf, buf->len);

  freeBuffer(buf);
  Py_DecRef(obj);

  return res;
};
