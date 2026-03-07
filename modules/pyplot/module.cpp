/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/other.h"
#include "camel/core/type/resolver.h"
#include "executor.h"
#include "operators.h"

#include <fstream>
#include <optional>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace py = pybind11;

static void set_python_home_from_venv() {
    std::string venv_path;
#ifdef _WIN32
    char *venv_env = nullptr;
    size_t len     = 0;
    if (_dupenv_s(&venv_env, &len, "VIRTUAL_ENV") == 0 && venv_env != nullptr) {
        venv_path = std::string(venv_env);
        free(venv_env);
    }
#else
    const char *venv_env = std::getenv("VIRTUAL_ENV");
    if (venv_env)
        venv_path = std::string(venv_env);
#endif
    if (venv_path.empty())
        return;
#ifdef _WIN32
    std::string cfg_path = venv_path + "\\pyvenv.cfg";
#else
    std::string cfg_path = venv_path + "/pyvenv.cfg";
#endif
    std::ifstream f(cfg_path);
    if (!f)
        return;
    std::string line, home_path;
    while (std::getline(f, line)) {
        if (line.find("home") == 0) {
            size_t eq = line.find('=');
            if (eq != std::string::npos) {
                home_path = line.substr(eq + 1);
                size_t s  = home_path.find_first_not_of(" \t");
                if (s != std::string::npos)
                    home_path = home_path.substr(s);
                break;
            }
        }
    }
    if (home_path.empty())
        return;
    wchar_t *whome = Py_DecodeLocale(home_path.c_str(), nullptr);
    if (!whome)
        return;
    static std::wstring python_home_storage;
    python_home_storage = whome;
    PyMem_RawFree(whome);
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
    Py_SetPythonHome(python_home_storage.c_str());
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
}

namespace {

const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "plot",
            {
                {
                    "pyplot:plot",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(data: (typeas T)[], filename?: string) => void",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1 || norm.size() > 2)
                                return std::nullopt;
                            if (norm[0]->code() != TypeCode::Array)
                                return std::nullopt;
                            TypeCode elemCode = tt::as_ptr<ArrayType>(norm[0])->elemType()->code();
                            if (elemCode != TypeCode::Int32 && elemCode != TypeCode::Int64 &&
                                elemCode != TypeCode::Float32 && elemCode != TypeCode::Float64)
                                return std::nullopt;
                            if (norm.size() == 2 && norm[1]->code() != TypeCode::String)
                                return std::nullopt;
                            return Type::Void();
                        }),
                },
            }),
    };
    return groups;
}

} // namespace

PyplotModule::PyplotModule(context_ptr_t ctx) : BuiltinModule("pyplot", ctx) {
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t PyplotModule::create(context_ptr_t ctx) { return std::make_shared<PyplotModule>(ctx); }

bool PyplotModule::load() {
    if (loaded_)
        return true;
    try {
        if (!Py_IsInitialized()) {
            set_python_home_from_venv();
            py::initialize_interpreter();
        }
    } catch (const std::exception &e) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("Failed to load pyplot module: ") + e.what());
    }
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("pyplot", [ctx]() { return createPyplotExecutor(ctx); });
    loaded_ = true;
    return true;
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new PyplotModule(ctx->shared_from_this()); }
}
