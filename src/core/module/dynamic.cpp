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
 * Created: Feb. 20, 2026
 * Updated: Feb. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/module/dynamic.h"
#include "camel/core/context/context.h"
#include "camel/utils/log.h"

#include <memory>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

void *openDll(const std::string &path) {
#ifdef _WIN32
    return static_cast<void *>(LoadLibraryA(path.c_str()));
#else
    return dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
}

void closeDll(void *handle) {
    if (!handle)
        return;
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

template <typename F> F getSymbol(void *handle, const char *name) {
#ifdef _WIN32
    return reinterpret_cast<F>(GetProcAddress(static_cast<HMODULE>(handle), name));
#else
    return reinterpret_cast<F>(dlsym(handle, name));
#endif
}

} // namespace

module_ptr_t
loadCmoModule(const std::string &moduleName, const std::string &path, const context_ptr_t &ctx) {

    void *handle = openDll(path);
    if (!handle) {
        EXEC_WHEN_DEBUG(l.in("Context").warn("Failed to load .cmo: {}", path));
        return nullptr;
    }

    auto factory = getSymbol<CamelModuleFactory>(handle, "camel_module_create");
    if (!factory) {
        EXEC_WHEN_DEBUG(l.in("Context").warn("camel_module_create not found in: {}", path));
        closeDll(handle);
        return nullptr;
    }

    Module *raw = factory(ctx.get());
    if (!raw) {
        closeDll(handle);
        return nullptr;
    }

    ctx->addLoadedDll(path, handle);
    return std::shared_ptr<Module>(raw);
}
