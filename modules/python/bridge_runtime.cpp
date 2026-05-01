/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Apr. 01, 2026
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
