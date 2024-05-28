#include <stdio.h>
#include <string.h>

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdiscarded-qualifiers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif

#include "common.h"
#include "ctx.h"
#include "str.h"

#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif

#define returnIfError(o)                                                                           \
  if (o)                                                                                           \
  return o

static HPy bencode(HPy mod, HPy obj);

// module level variable
PyObject *BencodeEncodeError;
PyDoc_STRVAR(__bencode_doc__, "bencode(v: Any, /) -> bytes\n"
                              "--\n\n"
                              "encode python object to bytes");
PyMethodDef encodeImpl[] = {{
                                .ml_name = "bencode",
                                .ml_meth = bencode,
                                .ml_flags = METH_O,
                                .ml_doc = __bencode_doc__,
                            },
                            {NULL, NULL, 0, NULL}};
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

static int encodeAny(Context *ctx, HPy obj);

typedef struct keyValuePair {
  char *key;
  Py_ssize_t keylen;

  PyObject *pyKey;
  PyObject *value;
} KeyValuePair;

int sortKeyValuePair(const void *a, const void *b) {
  struct keyValuePair *aa = (KeyValuePair *)a;
  struct keyValuePair *bb = (KeyValuePair *)b;

  return strCompare(aa->key, aa->keylen, bb->key, bb->keylen);
}

static void freeKeyValueList(KeyValuePair *list, HPy_ssize_t len) {
  for (HPy_ssize_t i = 0; i < len; i++) {
    debug_print("1 %lld", i);
    debug_print("key 0x%p", list[i].pyKey);
    Py_XDECREF(list[i].pyKey);
  }

  debug_print("free list");
  free(list);
}

static int buildDictKeyList(HPy obj, struct keyValuePair **pairs, HPy_ssize_t *count) {
  *count = PyDict_Size(obj);

  if (*count == 0) {
    return 0;
  }

  //  KeyValuePair *pp = calloc((*count), (sizeof(KeyValuePair)));
  KeyValuePair *pp = malloc((*count) * (sizeof(KeyValuePair)));
  *pairs = pp;

  HPy keys = PyDict_Keys(obj);
  if (keys == NULL) {
    runtimeError("failed to get dict keys");
    return 1;
  }

  for (HPy_ssize_t i = 0; i < *count; i++) {
    HPy key = PyList_GetItem(keys, i);
    if (key == NULL) {
      Py_DecRef(keys);
      runtimeError("failed to get key from dict");
      return 1;
    }

    HPy keyAsBytes = NULL;
    if (PyUnicode_Check(key)) {
      keyAsBytes = PyUnicode_AsUTF8String(key);
    } else if (!PyBytes_Check(key)) {
      bencodeError("dict key must be Str or bytes");
      Py_DecRef(keys);
      return 1;
    }

    HPy value = PyDict_GetItem(obj, key);
    if (value == NULL) {
      Py_DecRef(keys);
      runtimeError("failed to get value from dict");
      return 1;
    }

    if (keyAsBytes != NULL) {
      pp[i].key = PyBytes_AsString(keyAsBytes);
      pp[i].keylen = PyBytes_Size(keyAsBytes);
      pp[i].pyKey = keyAsBytes;
      //      Py_DecRef(keyAsBytes);
    } else {
      pp[i].key = PyBytes_AsString(key);
      pp[i].keylen = PyBytes_Size(key);
      pp[i].pyKey = NULL;
    }

    pp[i].value = value;
  }

  Py_DecRef(keys);

  qsort(pp, *count, sizeof(KeyValuePair), sortKeyValuePair);

  // check duplicated keys
  const char *lastKey = pp[0].key;
  size_t lastKeylen = pp[0].keylen;
  const char *currentKey = NULL;
  size_t currentKeylen;

  for (HPy_ssize_t i = 1; i < *count; i++) {
    KeyValuePair item = pp[i];

    currentKey = item.key;
    currentKeylen = item.keylen;

    if (lastKeylen == currentKeylen) {
      debug_print("lastKey=%s, currentKey=%s", lastKey, currentKey);
      if (strncmp(lastKey, currentKey, lastKeylen) == 0) {
        bencodeError("find duplicated keys with str and bytes in dict");
        return 1;
      }
    }

    lastKey = currentKey;
    lastKeylen = currentKeylen;
  }

  return 0;
}

#if PY_MINOR_VERSION >= 10
// TODO: use py buffer >= 3.11
static int encodeBytes(Context *ctx, HPy obj) {
  HPy_ssize_t size;
  char *data;
  if (PyBytes_AsStringAndSize(obj, &data, &size)) {
    return 1;
  }

  int err = bufferWriteFormat(ctx, "%zd", size);
  err = err || bufferWriteChar(ctx, ':');
  return err || bufferWrite(ctx, data, size);
}

static int encodeStr(Context *ctx, HPy obj) {
  HPy_ssize_t size;
  const char *data = PyUnicode_AsUTF8AndSize(obj, &size);
  if (data == NULL) {
    return 1;
  }

  int err = bufferWriteFormat(ctx, "%zd", size);
  err = err || bufferWriteChar(ctx, ':');
  return err || bufferWrite(ctx, data, size);
}

#else

// TODO: remove this after we drop py39
static int encodeBytes(Context *ctx, HPy obj) {
  const char *data = PyBytes_AsString(obj);
  if (data == NULL) {
    return 1;
  }

  HPy_ssize_t size = PyBytes_Size(obj);

  int err = bufferWriteFormat(ctx, "%zd", size);
  err = err || bufferWriteChar(ctx, ':');
  return err || bufferWrite(ctx, data, size);
}

static int encodeStr(Context *ctx, HPy obj) {
  HPy b = PyUnicode_AsUTF8String(obj);

  int err = encodeBytes(ctx, b);
  Py_DecRef(b);
  return err;
}

#endif

static int encodeDict(Context *ctx, HPy obj) {
  returnIfError(bufferWrite(ctx, "d", 1));

  struct keyValuePair *list = NULL;
  HPy_ssize_t count = 0;
  if (buildDictKeyList(obj, &list, &count)) {
    if (list != NULL) {
      free(list);
    }
    return 1;
  }

  if (count == 0) {
    return bufferWrite(ctx, "e", 1);
  }

  for (HPy_ssize_t i = 0; i < count; i++) {
    debug_print("encode key[%zd]", i);

    struct keyValuePair keyValue = list[i];
    int err = 0;

    err |= bufferWriteFormat(ctx, "%ld", keyValue.keylen);
    err |= bufferWriteChar(ctx, ':');
    err |= bufferWrite(ctx, keyValue.key, keyValue.keylen);
    err |= encodeAny(ctx, keyValue.value);

    if (err) {
      debug_print("failed");
      freeKeyValueList(list, count);
      return 1;
    }
  }

  debug_print("1");
  freeKeyValueList(list, count);
  debug_print("2");

  return bufferWrite(ctx, "e", 1);
}

static int encodeInt_slow(Context *ctx, HPy obj) {
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

  int err = bufferWrite(ctx, "i", 1);
  err = err || bufferWrite(ctx, data, size);

  Py_DecRef(fmt);
  Py_DecRef(s);
  Py_DecRef(b);

  return err || bufferWrite(ctx, "e", 1);
}

static int encodeInt_fast(Context *ctx, long long val) {
  returnIfError(bufferWrite(ctx, "i", 1));
  returnIfError(bufferWriteLongLong(ctx, val));
  return bufferWrite(ctx, "e", 1);
}

static int encodeInt(Context *ctx, HPy obj) {
  int overflow = 0;
  long long val = PyLong_AsLongLongAndOverflow(obj, &overflow);
  if (overflow) {
    PyErr_Clear();
    // slow path for very long int
    return encodeInt_slow(ctx, obj);
  }
  if (val == -1 && PyErr_Occurred()) { // unexpected error
    return 1;
  }

  return encodeInt_fast(ctx, val);
}

static int encodeList(Context *ctx, HPy obj) {
  HPy_ssize_t len = PyList_Size(obj);
  returnIfError(bufferWrite(ctx, "l", 1));

  for (HPy_ssize_t i = 0; i < len; i++) {
    HPy o = PyList_GetItem(obj, i);
    returnIfError(encodeAny(ctx, o));
  }

  return bufferWrite(ctx, "e", 1);
}

static int encodeTuple(Context *ctx, HPy obj) {
  HPy_ssize_t len = PyTuple_Size(obj);
  returnIfError(bufferWrite(ctx, "l", 1));

  for (HPy_ssize_t i = 0; i < len; i++) {
    HPy o = PyTuple_GetItem(obj, i);
    returnIfError(encodeAny(ctx, o));
  }

  return bufferWrite(ctx, "e", 1);
}

#define encodeComposeObject(ctx, obj, encoder)                                                     \
  do {                                                                                             \
    debug_print("put object %p to seen", obj);                                                     \
    int absent;                                                                                    \
    kh_put_PTR(ctx->seen, (khint64_t)obj, &absent);                                                \
    debug_print("after put object %p to seen", obj);                                               \
    if (!absent) {                                                                                 \
      debug_print("circular reference found");                                                     \
      PyErr_SetString(PyExc_ValueError, "circular reference found");                               \
      return 1;                                                                                    \
    }                                                                                              \
    int r = encoder(ctx, obj);                                                                     \
    khint64_t key = kh_get_PTR(ctx->seen, (khint64_t)obj);                                         \
    kh_del_PTR(ctx->seen, key);                                                                    \
    return r;                                                                                      \
  } while (0)

static int encodeAny(Context *ctx, HPy obj) {
  if (obj == Py_True) {
    return bufferWrite(ctx, "i1e", 3);
  }

  if (obj == Py_False) {
    return bufferWrite(ctx, "i0e", 3);
  }

  if (PyBytes_Check(obj)) {
    return encodeBytes(ctx, obj);
  }

  if (PyUnicode_Check(obj)) {
    return encodeStr(ctx, obj);
  }

  if (PyLong_Check(obj)) {
    return encodeInt(ctx, obj);
  }

  if (PyList_Check(obj)) {
    encodeComposeObject(ctx, obj, encodeList);
  }

  if (PyTuple_Check(obj)) {
    encodeComposeObject(ctx, obj, encodeTuple);
  }

  if (PyDict_Check(obj)) {
    encodeComposeObject(ctx, obj, encodeDict);
  }

  if (PyByteArray_Check(obj)) {
    HPy_ssize_t size = PyByteArray_Size(obj);
    const char *data = PyByteArray_AsString(obj);
    if (data == NULL) {
      return 1;
    }

    int err = bufferWriteFormat(ctx, "%zd", size);
    err = err || bufferWriteChar(ctx, ':');
    return err || bufferWrite(ctx, data, size);
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
  Context ctx = newContext(&bufferAlloc);
  if (bufferAlloc) {
    return NULL;
  }

  // error when encoding
  if (encodeAny(&ctx, obj)) {
    freeContext(ctx);
    return NULL;
  }

  HPy res = PyBytes_FromStringAndSize(ctx.buf, ctx.index);

  freeContext(ctx);

  return res;
}
