#define Py_LIMITED_API 0x03080000

// TODO: should use a extern here
#include "decode.h"
#include "encode.h"

static PyMethodDef DemoMethods[] = {
    {"bencode", bencode, METH_O, "encode python object to bytes"},
    {"bdecode", bdecode, METH_O, "decode bytes to python object"},
    {NULL, NULL, 0, NULL},
};

static PyModuleDef moduleDef = {
    PyModuleDef_HEAD_INIT, "_bencode", "Point module (Step 0; C API implementation)", -1, DemoMethods,
};

PyMODINIT_FUNC PyInit__bencode(void) {
  PyObject *m = PyModule_Create(&moduleDef);
  if (m == NULL)
    return NULL;

  BencodeDecodeError = PyErr_NewException("_bencode.BencodeDecodeError", NULL, NULL);
  Py_XINCREF(BencodeDecodeError);
  if (PyModule_AddObject(m, "BencodeDecodeError", BencodeDecodeError) < 0) {
    Py_XDECREF(BencodeDecodeError);
    Py_CLEAR(BencodeDecodeError);
    Py_DECREF(m);
    return NULL;
  }

  BencodeEncodeError = PyErr_NewException("_bencode.BencodeEncodeError", NULL, NULL);
  Py_XINCREF(BencodeEncodeError);
  if (PyModule_AddObject(m, "BencodeEncodeError", BencodeEncodeError) < 0) {
    Py_XDECREF(BencodeEncodeError);
    Py_CLEAR(BencodeEncodeError);
    Py_DECREF(m);
    return NULL;
  }
  return m;
}
