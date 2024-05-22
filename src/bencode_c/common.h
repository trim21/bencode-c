#ifndef MY_COMMON_H

#define Py_LIMITED_API 0x03070000
#include <Python.h>

#define HPy_ssize_t Py_ssize_t
#define HPy PyObject *

#define NON_SUPPORTED_TYPE_MESSAGE                                                                                     \
  "invalid type '%s', "                                                                                                \
  "bencode only support bytes, str, int, list, tuple, dict and bool(encoded as 0/1, decoded as int)"

#define MY_COMMON_H
#endif
