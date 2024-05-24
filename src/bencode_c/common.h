#pragma once

#include <Python.h>

#define HPy_ssize_t Py_ssize_t
#define HPy PyObject *

#define NON_SUPPORTED_TYPE_MESSAGE                                                                 \
  "invalid type '%s', "                                                                            \
  "bencode only support bytes, str, "                                                              \
  "int, list, tuple, dict and bool(encoded as 0/1, decoded as int)"

#ifdef BENCODE_DEBUG

#ifdef _MSC_VER
#define debug_print(fmt, ...)                                                                      \
  do {                                                                                             \
    printf(__FILE__);                                                                              \
    printf(":");                                                                                   \
    printf("%d", __LINE__);                                                                        \
    printf("\t%s", __FUNCTION__);                                                                  \
    printf("\tDEBUG: ");                                                                           \
    printf(fmt, __VA_ARGS__);                                                                      \
    printf("\n");                                                                                  \
  } while (0)

#else
#define debug_print(fmt, ...)                                                                      \
  do {                                                                                             \
    printf(__FILE__);                                                                              \
    printf(":");                                                                                   \
    printf("%d", __LINE__);                                                                        \
    printf("\t%s", __PRETTY_FUNCTION__);                                                           \
    printf("\tDEBUG: " #fmt "\n", ##__VA_ARGS__);                                                  \
  } while (0)

#endif
#else

#define debug_print(fmt, ...)

#endif
