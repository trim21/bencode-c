#include <stdio.h>
#include <string.h>

#include <khash.h>

#include "common.h"
#include "ctx.h"

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

static int encodeAny(Context *ctx, HPy obj);

typedef struct keyValuePair {
  char *key;
  int keylen;

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
    debug_print("1 %d\n", i);
    debug_print("kk %d\n", list[i].pyKey);
    Py_XDECREF(list[i].pyKey);
  }

  debug_print("free list\n");
  free(list);
}

static int buildDictKeyList(HPy obj, struct keyValuePair **pairs, HPy_ssize_t *count) {
  *count = PyObject_Length(obj);

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

    KeyValuePair item = pp[i];
  }

  for (HPy_ssize_t i = 0; i < *count; i++) {
    KeyValuePair item = pp[i];
  }

  Py_DecRef(keys);

  for (HPy_ssize_t i = 0; i < *count; i++) {
    KeyValuePair item = pp[i];
  }

  qsort(pp, *count, sizeof(KeyValuePair), sortKeyValuePair);

  for (HPy_ssize_t i = 0; i < *count; i++) {
    KeyValuePair item = pp[i];
  }

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
      debug_print("lastKey=%s, currentKey=%s\n", lastKey, currentKey);
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

// TODO: use PyUnicode_AsUTF8AndSize after 3.10
static int encodeStr(Context *ctx, HPy obj) {
  HPy b = PyUnicode_AsUTF8String(obj);

  int err = encodeBytes(ctx, b);
  Py_DecRef(b);
  return err;
}

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
    debug_print("encode %d\n", i);

    struct keyValuePair keyValue = list[i];
    int err = 0;

    err |= bufferWriteFormat(ctx, "%d", keyValue.keylen);
    err |= bufferWriteChar(ctx, ':');
    err |= bufferWrite(ctx, keyValue.key, keyValue.keylen);
    err |= encodeAny(ctx, keyValue.value);

    if (err) {
      debug_print("failed\n");
      freeKeyValueList(list, count);
      return 1;
    }
  }

  debug_print("1\n");
  freeKeyValueList(list, count);
  debug_print("2\n");

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

KHASH_MAP_INIT_INT(m32, char) // instantiate structs and methods

static int encodeAny(Context *ctx, HPy obj) {
//  unsigned long long ptr = (unsigned long long)obj;
//  //  debug_print("%d\n", (long long)obj);
//  //  debug_print("%d\n", (long)obj);
//  //  debug_print("%d\n", (size_t)obj);
//  //  debug_print("ptr=%d\n", ptr);
//  //  debug_print("encodeAny len(b.seen)=%d\n", kh_size(ctx->seen));
//  //  debug_print("encodeAny b.seen=%d\n", ctx->seen);
//  khint_t sk = kh_get(PyObject, ctx->seen, ptr);
//  debug_print("set key=%d\n", sk);
//  debug_print("set end=%d\n", kh_end(ctx->seen));
//  if (sk != kh_end(ctx->seen)) {
//    PyErr_SetString(PyExc_ValueError, "recursive object found");
//    return 1;
//  }
//  debug_print("put set key=%d\n", sk);
  //  int absent;
  //  int ret, is_missing;
  //  kh_put(PyObject, ctx->seen, sk, &ret);
  {
    int ret, is_missing;
    khint_t k;
    khash_t(m32) *h = kh_init(m32); // allocate a hash table
    k = kh_put(m32, h, 5, &ret);    // insert a key to the hash table
    if (!ret) {
      kh_del(m32, h, k);
    }
    kh_value(h, k) = 10;           // set the value
    k = kh_get(m32, h, 10);        // query the hash table
    is_missing = (k == kh_end(h)); // test if the key is present
    k = kh_get(m32, h, 5);
    kh_del(m32, h, k);                           // remove a key-value pair
    for (k = kh_begin(h); k != kh_end(h); ++k) { // traverse
      if (kh_exist(h, k)) {                      // test if a bucket contains data
        kh_value(h, k) = 1;
      }
    }
    kh_destroy(m32, h); // deallocate the hash table
  }

  if (Py_True == obj) {
    return bufferWrite(ctx, "i1e", 3);
  }

  if (Py_False == obj) {
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
    return encodeList(ctx, obj);
  }

  if (PyTuple_Check(obj)) {
    return encodeTuple(ctx, obj);
  }

  if (PyDict_Check(obj)) {
    return encodeDict(ctx, obj);
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
