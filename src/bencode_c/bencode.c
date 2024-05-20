#include "Python.h"

// TODO: should use a extern here
#include "decode.h"
#include "encode.h"

// extern HPyDef bencode;

// static PyObject *bdecode(PyObject *self, PyObject *obj);

// extern HPyGlobal BencodeDecodeError;
// extern HPyGlobal BencodeEncodeError;

// static bool init_exception(HPyContext *ctx, HPy mod, HPyGlobal *global,
//                            const char *name, const char *attr_name) {
//   HPy h = HPyErr_NewException(ctx, name, HPy_NULL, HPy_NULL);
//   if (HPy_IsNull(h)) {
//     return false;
//   }
//   HPyGlobal_Store(ctx, global, h);
//   int set_attr_failed = HPy_SetAttr_s(ctx, mod, attr_name, h);
//   HPy_Close(ctx, h);
//   return !set_attr_failed;
// }

// bool init_exceptions(HPyContext *ctx, HPy mod) {
//   init_exception(ctx, mod, &BencodeDecodeError,
//   "_bencode.BencodeDecodeError",
//                  "BencodeDecodeError");
//   return init_exception(ctx, mod, &BencodeEncodeError,
//                         "_bencode.BencodeEncodeError", "BencodeEncodeError");
//   // return true;
// }

// HPyDef_SLOT(bencode_exec,
//             HPy_mod_exec) static int bencode_exec_impl(HPyContext *ctx,
//                                                        HPy module) {
//   if (!init_exceptions(ctx, module)) {
//     HPyErr_SetString(ctx, ctx->h_RuntimeError, "failed to init exceptions");
//     return 1;
//   }
//   return 0;
// }

// static HPyDef *module_defines[] = {&bencode_exec, &bdecode, &bencode, NULL};

// module's method table

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
