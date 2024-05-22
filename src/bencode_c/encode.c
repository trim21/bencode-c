#include "stdio.h"
#include "string.h"

#include "common.h"

#define defaultBufferSize 4096

#define returnIfError(o)                                                                           \
  if (o) {                                                                                         \
    return o;                                                                                      \
  }

#define OperatorIF(err, op)                                                                        \
  if (!err) {                                                                                      \
    err |= op;                                                                                     \
  }

static HPy bencode(HPy mod, HPy obj);

// module level variable
PyObject *BencodeEncodeError;
PyDoc_STRVAR(__bencode_doc__, "bencode(v: Any, /) -> bytes\n"
                              "--\n\n"
                              "encode python object to bytes");
PyMethodDef encodeImpl = {
    .ml_name = "bencode",
    .ml_meth = bencode,
    .ml_flags = METH_O,
    .ml_doc = __bencode_doc__,
};
// module level variable

HPy errTypeMessage;

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

static int encodeAny(struct buffer *buf, HPy obj);

// obj must be a python dict object.
// TODO: use c native struct and sorting
static int buildDictKeyList(HPy obj, HPy *list, HPy_ssize_t *count) {
  *count = PyObject_Length(obj);

  if (*count == 0) {
    return 0;
  }

  HPy keys = PyDict_Keys(obj);
  if (keys == NULL) {
    runtimeError("failed to get dict keys");
    return 1;
  }

  *list = PyList_New(0);

  for (HPy_ssize_t i = 0; i < *count; i++) {
    HPy key = PySequence_GetItem(keys, i);
    if (key == NULL) {
      Py_DecRef(keys);
      runtimeError("failed to get key from dict");
      return 1;
    }

    HPy keyAsBytes = key;
    if (PyUnicode_Check(key)) {
      keyAsBytes = PyUnicode_AsUTF8String(key);
    } else if (!PyBytes_Check(key)) {
      bencodeError("dict key must be str or bytes");
      Py_DecRef(keys);
      Py_DecRef(key);
      return 1;
    }

    HPy value = PyDict_GetItem(obj, key);
    if (value == NULL) {
      Py_DecRef(key);
      Py_DecRef(keys);
      runtimeError("failed to get value from dict");
      return 1;
    }

    HPy tu = PyTuple_Pack(2, keyAsBytes, value);
    if (tu == NULL) {
      runtimeError("can not pack key value pair");
      return 1;
    }

    PyList_Append(*list, tu);
    Py_DecRef(tu);
    Py_DecRef(key);

    if (keyAsBytes != key) {
      Py_DecRef(keyAsBytes);
    }
  }

  Py_DecRef(keys);

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

// TODO: use PyUnicode_AsUTF8AndSize after 3.10
static int encodeStr(struct buffer *buf, HPy obj) {
  HPy b = PyUnicode_AsUTF8String(obj);

  HPy_ssize_t size = PyBytes_Size(b);

  const char *data = PyBytes_AsString(b);
  if (data == NULL) {
    Py_DecRef(b);
    return 1;
  }

  int err = bufferWriteLong(buf, size);
  OperatorIF(err, bufferWrite(buf, ":", 1));
  OperatorIF(err, bufferWrite(buf, data, size));

  Py_DecRef(b);

  return err;
}

static int encodeBytes(struct buffer *buf, HPy obj) {
  HPy_ssize_t size = PyBytes_Size(obj);
  const char *data = PyBytes_AsString(obj);

  returnIfError(bufferWriteLong(buf, size));
  returnIfError(bufferWrite(buf, ":", 1));
  return bufferWrite(buf, data, size);
}

static int encodeDict(struct buffer *buf, HPy obj) {
  returnIfError(bufferWrite(buf, "d", 1));

  HPy list = NULL;
  HPy_ssize_t count = 0;
  if (buildDictKeyList(obj, &list, &count)) {
    if (list != NULL) {
      Py_DecRef(list);
    }
    return 1;
  }

  if (count == 0) {
    return bufferWrite(buf, "e", 1);
  }

  for (HPy_ssize_t i = 0; i < count; i++) {
    HPy keyValue = PyList_GetItem(list, i); // tuple[bytes, Any]
    if (keyValue == NULL) {
      Py_DecRef(list);
      runtimeError("failed to get key/value tuple from list");
      return 1;
    }

    HPy key = PyTuple_GetItem(keyValue, 0);
    if (key == NULL) {
      Py_DecRef(list);
      runtimeError("can't get key from key,value tuple");
      return 1;
    }

    if (encodeBytes(buf, key)) {
      Py_DecRef(list);
      return 1;
    }

    HPy value = PyTuple_GetItem(keyValue, 1);
    if (value == NULL) {
      Py_DecRef(list);
      runtimeError("can't get value");
      return 1;
    }

    if (encodeAny(buf, value)) {
      Py_DecRef(list);
      return 1;
    }
  }

  Py_DecRef(list);
  return bufferWrite(buf, "e", 1);
}

static int encodeInt(struct buffer *buf, HPy obj) {
  long long val = PyLong_AsLongLong(obj);
  if (PyErr_Occurred()) { // python int may overflow c long long.
    return 1;
  }

  returnIfError(bufferWrite(buf, "i", 1));
  returnIfError(bufferWriteLong(buf, val));
  return bufferWrite(buf, "e", 1);
}

static int encodeList(struct buffer *buf, HPy obj) {
  HPy_ssize_t len = PyList_Size(obj);
  returnIfError(bufferWrite(buf, "l", 1));

  for (HPy_ssize_t i = 0; i < len; i++) {
    HPy o = PyList_GetItem(obj, i);
    returnIfError(encodeAny(buf, o));
  }

  return bufferWrite(buf, "e", 1);
}

static int encodeTuple(struct buffer *buf, HPy obj) {
  HPy_ssize_t len = PyTuple_Size(obj);
  returnIfError(bufferWrite(buf, "l", 1));

  for (HPy_ssize_t i = 0; i < len; i++) {
    HPy o = PyTuple_GetItem(obj, i);
    returnIfError(encodeAny(buf, o));
  }

  return bufferWrite(buf, "e", 1);
}

static int encodeAny(struct buffer *buf, HPy obj) {
  if (Py_True == obj) {
    return bufferWrite(buf, "i1e", 3);
  }

  if (Py_False == obj) {
    return bufferWrite(buf, "i0e", 3);
  }

  if (PyBytes_Check(obj)) {
    return encodeBytes(buf, obj);
  }

  if (PyUnicode_Check(obj)) {
    return encodeStr(buf, obj);
  }

  if (PyLong_Check(obj)) {
    return encodeInt(buf, obj);
  }

  if (PyList_Check(obj)) {
    return encodeList(buf, obj);
  }

  if (PyTuple_Check(obj)) {
    return encodeTuple(buf, obj);
  }

  if (PyDict_Check(obj)) {
    return encodeDict(buf, obj);
  }

  // Unsupported type, raise TypeError

  HPy typ = PyObject_Type(obj);
  if (typ == NULL) {
    runtimeError("failed to get type of object");
    return 1;
  }

  HPy ss = PyUnicode_Format(errTypeMessage, typ);
  if (ss == NULL) {
    Py_DecRef(typ);
    runtimeError("failed to get type of object");
    return 1;
  }

  PyErr_SetObject(PyExc_TypeError, ss);

  Py_DecRef(ss);
  Py_DecRef(typ);

  return 1;
}

// mod is the module object
static HPy bencode(HPy mod, HPy obj) {
  struct buffer *buf = newBuffer();

  // error when encoding
  if (encodeAny(buf, obj)) {
    freeBuffer(buf);
    return NULL;
  }

  HPy res = PyBytes_FromStringAndSize(buf->buf, buf->len);

  freeBuffer(buf);

  return res;
};
