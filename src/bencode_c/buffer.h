#pragma once

#include "common.h"
#include "str.h"

#define defaultBufferSize 4096

struct Buffer {
  char *buf;
  size_t index;
  size_t cap;
};

static int bufferWriteFormat(struct Buffer *buf, char *format, ...)
    __attribute__((format(printf, 2, 3)));

static struct Buffer newBuffer(int *res) {
  struct Buffer b = {};

  b.buf = (char *)malloc(defaultBufferSize);
  if (b.buf == NULL) {
    PyErr_SetNone(PyExc_MemoryError);
    *res = 1;
    return b;
  }

  b.index = 0;
  b.cap = defaultBufferSize;

  return b;
}

static int bufferGrow(struct Buffer *buf, HPy_ssize_t size) {
  if (size + buf->index + 1 >= buf->cap) {
    void *tmp = realloc(buf->buf, buf->cap * 2 + size);
    if (tmp == NULL) {
      PyErr_SetString(PyExc_MemoryError, "failed to grow buffer");
      return 1;
    }
    buf->cap = buf->cap * 2 + size;
    buf->buf = (char *)tmp;
  }

  return 0;
}

static int bufferWrite(struct Buffer *buf, const char *data, HPy_ssize_t size) {
  if (bufferGrow(buf, size)) {
    return 1;
  }

  memcpy(buf->buf + buf->index, data, size);

  buf->index = buf->index + size;

  return 0;
}

static int bufferWriteChar(struct Buffer *buf, const char c) {
  if (bufferGrow(buf, 1)) {
    return 1;
  }
  buf->buf[buf->index] = c;
  buf->index = buf->index + 1;
  return 0;
}

static int bufferWriteFormat(struct Buffer *buf, char *format, ...) {
  va_list args, args2;

  va_start(args, format);
  va_copy(args2, args);
  Py_ssize_t size = vsnprintf(NULL, 0, format, args);

  if (bufferGrow(buf, size + 1)) {
    va_end(args);
    va_end(args2);
    return 1;
  }

  size = vsnprintf(&buf->buf[buf->index], size + 1, format, args2);
  if (size < 0) {
    va_end(args);
    va_end(args2);
    PyErr_SetString(PyExc_RuntimeError, "vsnprintf return unexpected value");
    return 1;
  }
  va_end(args);
  va_end(args2);

  buf->index += size;

  return 0;
}

static inline int bufferWriteLongLong(struct Buffer *buf, long long val) {
  return bufferWriteFormat(buf, "%lld", val);
}

static void freeBuffer(struct Buffer buf) { free(buf.buf); }
