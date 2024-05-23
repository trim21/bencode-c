#pragma once

#include <Python.h>

#define HPy_ssize_t Py_ssize_t
#define HPy PyObject *

#define NON_SUPPORTED_TYPE_MESSAGE                                                                 \
  "invalid type '%s', "                                                                            \
  "bencode only support bytes, Str, "                                                              \
  "int, list, tuple, dict and bool(encoded as 0/1, decoded as int)"
