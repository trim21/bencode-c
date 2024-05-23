#include <stdio.h>
#include <string.h>

#include "buffer.h"
#include "common.h"

#define returnIfError(o)                                                                           \
  if (o)                                                                                           \
  return o

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

static inline void runtimeError(const char *data) {
  PyErr_SetString(PyExc_RuntimeError, data);
  return;
}

static inline HPy bencodeError(const char *data) {
  PyErr_SetString(BencodeEncodeError, data);
  return NULL;
}

static int encodeAny(struct Buffer *buf, HPy obj);

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
      bencodeError("dict key must be Str or bytes");
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
  size_t currentKeylen;
  const char *lastKey = NULL;
  size_t lastKeylen;

  for (HPy_ssize_t i = 0; i < *count; i++) {
    HPy keyValue = PyList_GetItem(*list, i);
    HPy key = PyTuple_GetItem(keyValue, 0);
    currentKeylen = PyBytes_Size(key);
    currentKey = PyBytes_AsString(key);
    if (lastKey != NULL) {
      if (lastKeylen == currentKeylen) {
        if (strncmp(lastKey, currentKey, lastKeylen) == 0) {
          bencodeError("find duplicated keys with Str and bytes in dict");
          return 1;
        }
      }
    }

    lastKey = currentKey;
    lastKeylen = currentKeylen;
  }

  return 0;
}

static int encodeBytes(struct Buffer *buf, HPy obj) {
  const char *data = PyBytes_AsString(obj);
  if (data == NULL) {
    return 1;
  }

  HPy_ssize_t size = PyBytes_Size(obj);

  int err = bufferWriteFormat(buf, "%lld", size);
  err = err || bufferWriteChar(buf, ':');
  return err || bufferWrite(buf, data, size);
}

// TODO: use PyUnicode_AsUTF8AndSize after 3.10
static int encodeStr(struct Buffer *buf, HPy obj) {
  HPy b = PyUnicode_AsUTF8String(obj);

  int err = encodeBytes(buf, b);
  Py_DecRef(b);
  return err;
}

static int encodeDict(struct Buffer *buf, HPy obj) {
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

static int encodeInt_slow(struct Buffer *buf, HPy obj) {
  HPy fmt = PyUnicode_FromString("%d");
  HPy s = PyUnicode_Format(fmt, obj); // s = '%d" % i
  if (s == NULL) {
    Py_DecRef(fmt);
    return 1;
  }

  // TODO: use PyUnicode_AsUTF8String in py>=3.10
  HPy b = PyUnicode_AsUTF8String(s); // b = s.encode()
  if (b == NULL) {
    Py_DecRef(fmt);
    Py_DecRef(s);
    return 1;
  }

  HPy_ssize_t size = PyBytes_Size(b); // len(b)
  const char *data = PyBytes_AsString(b);
  if (data == NULL) {
    Py_DecRef(fmt);
    Py_DecRef(s);
    Py_DecRef(b);
    return 1;
  }

  int err = bufferWrite(buf, "i", 1);
  err = err || bufferWrite(buf, data, size);

  Py_DecRef(fmt);
  Py_DecRef(s);
  Py_DecRef(b);

  return err || bufferWrite(buf, "e", 1);
}

static int encodeInt_fast(struct Buffer *buf, long long val) {
  returnIfError(bufferWrite(buf, "i", 1));
  returnIfError(bufferWriteLongLong(buf, val));
  return bufferWrite(buf, "e", 1);
}

static int encodeInt(struct Buffer *buf, HPy obj) {
  int overflow = 0;
  long long val = PyLong_AsLongLongAndOverflow(obj, &overflow);
  if (overflow) {
    PyErr_Clear();
    // slow path for very long int
    return encodeInt_slow(buf, obj);
  }
  if (val == -1 && PyErr_Occurred()) { // unexpected error
    return 1;
  }

  return encodeInt_fast(buf, val);
}

static int encodeList(struct Buffer *buf, HPy obj) {
  HPy_ssize_t len = PyList_Size(obj);
  returnIfError(bufferWrite(buf, "l", 1));

  for (HPy_ssize_t i = 0; i < len; i++) {
    HPy o = PyList_GetItem(obj, i);
    returnIfError(encodeAny(buf, o));
  }

  return bufferWrite(buf, "e", 1);
}

static int encodeTuple(struct Buffer *buf, HPy obj) {
  HPy_ssize_t len = PyTuple_Size(obj);
  returnIfError(bufferWrite(buf, "l", 1));

  for (HPy_ssize_t i = 0; i < len; i++) {
    HPy o = PyTuple_GetItem(obj, i);
    returnIfError(encodeAny(buf, o));
  }

  return bufferWrite(buf, "e", 1);
}

static int encodeAny(struct Buffer *buf, HPy obj) {
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
  int bufferAlloc = 0;
  struct Buffer buf = newBuffer(&bufferAlloc);
  if (bufferAlloc) {
    return NULL;
  }

  // error when encoding
  if (encodeAny(&buf, obj)) {
    freeBuffer(buf);
    return NULL;
  }

  HPy res = PyBytes_FromStringAndSize(buf.buf, buf.index);

  freeBuffer(buf);

  return res;
}
