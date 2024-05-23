#pragma once

#include "common.h"
#include "str.h"

#define defaultBufferSize 4096

struct Buffer {
  char *buf;
  size_t len;
  size_t cap;
};

static struct Buffer newBuffer(int *res) {
  struct Buffer b = {};

  b.buf = (char *)malloc(defaultBufferSize);
  if (b.buf == NULL) {
    PyErr_SetNone(PyExc_MemoryError);
    *res = 1;
    return b;
  }

  b.len = 0;
  b.cap = defaultBufferSize;

  return b;
}

static int bufferWrite(struct Buffer *buf, const char *data, HPy_ssize_t size) {
  void *tmp;

  if (size + buf->len >= buf->cap) {
    tmp = realloc(buf->buf, buf->cap * 2 + size);
    if (tmp == NULL) {
      PyErr_SetNone(PyExc_MemoryError);
      return 1;
    }
    buf->cap = buf->cap * 2 + size;
    buf->buf = (char *)tmp;
  }

  memcpy(buf->buf + buf->len, data, size);

  buf->len = buf->len + size;

  return 0;
}

static int bufferWriteSize_t(struct Buffer *buf, HPy_ssize_t val) {
  struct Str s = {};

  if (str_printf(&s, "%d", val)) {
    return 1;
  }

  int r = bufferWrite(buf, s.str, s.size);
  free(s.str);
  return r;
}

static int bufferWriteLongLong(struct Buffer *buf, long long val) {
  struct Str s = {};
  if (str_printf(&s, "%lld", val)) {
    return 1;
  }

  int r = bufferWrite(buf, s.str, s.size);
  free(s.str);
  return r;
}

static void freeBuffer(struct Buffer buf) { free(buf.buf); }
