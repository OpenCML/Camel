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
 * Updated: Feb. 23, 2026
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

module_ptr_t loadCmoModule(
    const std::string &moduleName, const std::string &path, const context_ptr_t &ctx,
    std::string *outError) {
    auto setErr = [&](const std::string &s) {
        if (outError)
            *outError = s;
    };

    void *handle = openDll(path);
    if (!handle) {
        EXEC_WHEN_DEBUG(GetDefaultLogger().in("Context").warn("Failed to load .cmo: {}", path));
        setErr("failed to load DLL: " + path);
        return nullptr;
    }

    using AbiVersionFn = int (*)();
    auto abiVersionFn  = getSymbol<AbiVersionFn>(handle, "camel_module_abi_version");
    if (!abiVersionFn) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("Context").warn(
                "camel_module_abi_version not found in: {} (rebuild .cmo with current SDK)",
                path));
        closeDll(handle);
        setErr(
            "camel_module_abi_version not found in " + path + " (rebuild .cmo with current SDK)");
        return nullptr;
    }
    int moduleAbi = abiVersionFn();
    if (moduleAbi != CAMEL_MODULE_ABI_VERSION) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("Context").warn(
                "Module ABI version mismatch: {} has version {}, host expects {}; skip loading",
                path,
                moduleAbi,
                CAMEL_MODULE_ABI_VERSION));
        closeDll(handle);
        setErr(
            "ABI version mismatch: .cmo has version " + std::to_string(moduleAbi) +
            ", host expects " + std::to_string(CAMEL_MODULE_ABI_VERSION));
        return nullptr;
    }

    auto factory = getSymbol<CamelModuleFactory>(handle, "camel_module_create");
    if (!factory) {
        EXEC_WHEN_DEBUG(
            GetDefaultLogger().in("Context").warn("camel_module_create not found in: {}", path));
        closeDll(handle);
        setErr("camel_module_create not found in " + path);
        return nullptr;
    }

    Module *raw = factory(ctx.get());
    if (!raw) {
        closeDll(handle);
        setErr("camel_module_create returned null for " + path);
        return nullptr;
    }

    ctx->addLoadedDll(path, handle);
    return std::shared_ptr<Module>(raw);
}
