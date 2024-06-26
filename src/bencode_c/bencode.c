#include "common.h"

extern HPy errTypeMessage;
extern PyMethodDef encodeImpl[];
extern HPy BencodeEncodeError;

extern PyMethodDef decodeImpl[];
extern HPy BencodeDecodeError;

#if PY_MINOR_VERSION >= 13
static struct PyModuleDef_Slot my_module_slots[] = {{Py_mod_gil, Py_MOD_GIL_USED}, {0, NULL}};
#endif

static PyModuleDef moduleDef = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "_bencode",
    .m_doc = "bit-torrent bencode library",
    .m_size = -1,
#if PY_MINOR_VERSION >= 13
    .m_slots = my_module_slots,
#endif
};

PyMODINIT_FUNC PyInit__bencode(void) {
  PyObject *m = PyModule_Create(&moduleDef);
  if (m == NULL) {
    return NULL;
  }

  if (PyModule_AddIntConstant(m, "__BUILD_PY_MINOR_VERSION__", PY_MINOR_VERSION)) {
    return NULL;
  }

  if (PyModule_AddFunctions(m, encodeImpl)) {
    return NULL;
  }

  if (PyModule_AddFunctions(m, decodeImpl)) {
    return NULL;
  }

  errTypeMessage = PyUnicode_FromString(NON_SUPPORTED_TYPE_MESSAGE);
  Py_XINCREF(errTypeMessage);
  if (errTypeMessage == NULL) {
    Py_DECREF(m);
    return NULL;
  }

  BencodeDecodeError = PyErr_NewException("bencode_c.BencodeDecodeError", NULL, NULL);
  Py_XINCREF(BencodeDecodeError);
  if (PyModule_AddObject(m, "BencodeDecodeError", BencodeDecodeError) < 0) {
    Py_XDECREF(BencodeDecodeError);
    Py_CLEAR(BencodeDecodeError);
    Py_DECREF(m);
    return NULL;
  }

  BencodeEncodeError = PyErr_NewException("bencode_c.BencodeEncodeError", NULL, NULL);
  Py_XINCREF(BencodeEncodeError);
  if (PyModule_AddObject(m, "BencodeEncodeError", BencodeEncodeError) < 0) {
    Py_XDECREF(BencodeEncodeError);
    Py_CLEAR(BencodeEncodeError);
    Py_DECREF(m);
    return NULL;
  }

  return m;
}
