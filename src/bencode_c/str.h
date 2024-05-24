#pragma once

#include <stdarg.h>
#include <stdio.h>

#include <Python.h>

typedef struct str {
  char *str;
  Py_ssize_t size;
} Str;

static int va_str_printf(Str *ss, const char *format, va_list args) {
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
