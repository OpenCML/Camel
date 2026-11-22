// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include <Python.h>

#if defined(_WIN32)
#define CAMEL_BRIDGE_EXPORT __declspec(dllexport)
#else
#define CAMEL_BRIDGE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {

CAMEL_BRIDGE_EXPORT int camel_python_bridge_abi_version() { return 1; }

CAMEL_BRIDGE_EXPORT const char *camel_python_bridge_compiled_version() { return PY_VERSION; }

CAMEL_BRIDGE_EXPORT int camel_python_bridge_initialize() {
    if (!Py_IsInitialized()) {
        Py_Initialize();
    }
    return Py_IsInitialized() ? 1 : 0;
}

} // extern "C"
