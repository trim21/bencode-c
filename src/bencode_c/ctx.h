#pragma once

#include "common.h"
#include "str.h"

#include "khash.h"

#define defaultBufferSize 4096

// Check windows
#if _WIN32 || _WIN64
#if _WIN64
#define ENVIRONMENT64
#else
#define ENVIRONMENT32
#endif
#endif

// Check GCC
#if __GNUC__
#if __x86_64__ || __ppc64__
#define ENVIRONMENT64
#else
#define ENVIRONMENT32
#endif
#endif

KHASH_SET_INIT_INT64(PyObject);
//  #ifdef ENVIRONMENT64
//  #else
//  KHASH_SET_INIT_INT(pyObj);
//  #endif

static int setKeyAbsent = 0;

typedef struct ctx {
  char *buf;
  size_t index;
  size_t cap;
  khash_t(PyObject) * seen;
} Context;

#ifdef _MSC_VER
static int bufferWriteFormat(Context *ctx, _Printf_format_string_ const char *format, ...);
#else
static int bufferWriteFormat(Buffer *buf, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
#endif

static Context newContext(int *res) {
  //  Context b = {.seen = NULL};
  Context b = {};

  b.buf = (char *)malloc(defaultBufferSize);
  if (b.buf == NULL) {
    PyErr_SetNone(PyExc_MemoryError);
    *res = 1;
    return b;
  }

  b.index = 0;
  b.cap = defaultBufferSize;
  b.seen = kh_init(PyObject);

  //  debug_print("newContext b.seen=%d\n", b.seen);

  return b;
}

static void freeContext(Context ctx) {
  if (ctx.seen != NULL) {
    kh_destroy(PyObject, ctx.seen);
  }
  free(ctx.buf);
}

static int bufferGrow(Context *ctx, HPy_ssize_t size) {
  if (size + ctx->index + 1 >= ctx->cap) {
    void *tmp = realloc(ctx->buf, ctx->cap * 2 + size);
    if (tmp == NULL) {
      PyErr_SetString(PyExc_MemoryError, "failed to grow buffer");
      return 1;
    }
    ctx->cap = ctx->cap * 2 + size;
    ctx->buf = (void *)tmp;
  }

  return 0;
}

static int bufferWrite(Context *ctx, const char *data, HPy_ssize_t size) {
  if (bufferGrow(ctx, size)) {
    return 1;
  }

  memcpy(ctx->buf + ctx->index, data, size);

  ctx->index = ctx->index + size;

  return 0;
}

static int bufferWriteChar(Context *buf, const char c) {
  if (bufferGrow(buf, 1)) {
    return 1;
  }
  buf->buf[buf->index] = c;
  buf->index = buf->index + 1;
  return 0;
}

static int bufferWriteFormat(Context *ctx, const char *format, ...) {
  va_list args, args2;

  va_start(args, format);
  va_copy(args2, args);
  Py_ssize_t size = vsnprintf(NULL, 0, format, args);

  if (bufferGrow(ctx, size + 1)) {
    va_end(args);
    va_end(args2);
    return 1;
  }

  size = vsnprintf(&ctx->buf[ctx->index], size + 1, format, args2);
  if (size < 0) {
    va_end(args);
    va_end(args2);
    PyErr_SetString(PyExc_RuntimeError, "vsnprintf return unexpected value");
    return 1;
  }
  va_end(args);
  va_end(args2);

  ctx->index += size;

  return 0;
}

static inline int bufferWriteLongLong(Context *ctx, long long val) {
  return bufferWriteFormat(ctx, "%lld", val);
}
