#pragma once

#include <Python.h>

#define HPy_ssize_t Py_ssize_t
#define HPy PyObject *

#define NON_SUPPORTED_TYPE_MESSAGE                                                                 \
  "invalid type '%s', "                                                                            \
  "bencode only support bytes, Str, "                                                              \
  "int, list, tuple, dict and bool(encoded as 0/1, decoded as int)"

#ifdef BENCODE_DEBUG

#define debug_print(fmt, ...)                                                                      \
  do {                                                                                             \
    fprintf(stderr, fmt, __VA_ARGS__);                                                             \
  } while (0)

#else

#define debug_print(fmt, ...)

#endif
