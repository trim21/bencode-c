#pragma once

#include <stdarg.h>
#include <stdio.h>

#include <Python.h>

struct str {
  char *str;
  Py_ssize_t size;
} str;

static int va_str_printf(struct str *ss, const char *format, va_list args) {
  size_t size = vsnprintf(NULL, 0, format, args) + 1;
  if (size == 0) {
    PyErr_SetString(PyExc_RuntimeError, "snprintf return unexpected value");
    return 1;
  }

  char *s = calloc(size, sizeof(char));
  if (s == NULL) {
    PyErr_SetString(PyExc_MemoryError, "failed to alloc memory");
    return 1;
  }

  vsnprintf(s, size, format, args);

  ss->str = s;
  ss->size = size - 1;

  return 0;
}

static int str_printf(struct str *ss, const char *format, ...) {
  va_list args;

  va_start(args, format);
  int r = va_str_printf(ss, format, args);
  va_end(args);

  return r;
}
