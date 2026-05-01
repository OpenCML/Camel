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
 * Created: Feb. 23, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "module.h"

#include "camel/core/context/context.h"

namespace camel::core::module {

/** ABI version between the host and a .cmo module; reject mismatches to avoid crashes from
 *
 * interpreter/module version drift. */
#define CAMEL_MODULE_ABI_VERSION 1

/** Factory that a .cmo dynamic library must implement: create and return a module instance from
 *
 * context (caller takes ownership). */
using CamelModuleFactory = Module *(*)(camel::core::context::Context *);

#ifdef __cplusplus
extern "C" {
#endif
/** The .cmo must export this: returns CAMEL_MODULE_ABI_VERSION for pre-load version checks. */
int camel_module_abi_version(void);
/** Symbol required by third-party .cmo modules: returns a new-allocated Module* that the runtime
 *
 * will take over via shared_ptr. */
Module *camel_module_create(camel::core::context::Context *ctx);
#ifdef __cplusplus
}
#endif

/** Load a dynamic library from a .cmo path and call camel_module_create; return the module. The
 *
 * DLL handle is owned by ctx. Returns nullptr on failure; if outError is non-null, write the
 *
 * failure reason (e.g. DLL load failure or ABI mismatch). */
module_ptr_t loadCmoModule(
    const std::string &moduleName, const std::string &path,
    const camel::core::context::context_ptr_t &ctx, std::string *outError = nullptr);

} // namespace camel::core::module
