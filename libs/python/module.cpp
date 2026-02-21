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
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "executor.h"
#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/other.h"
#include "camel/core/type/resolver.h"
#include "operators.h"

#include <optional>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace py = pybind11;

// 确保 Python 解释器能够找到虚拟环境中的包
static void ensure_python_path() {
    try {
        if (!Py_IsInitialized())
            return;

        py::module_ sys  = py::module_::import("sys");
        py::module_ site = py::module_::import("site");
        py::module_ os   = py::module_::import("os");

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

        if (!venv_path.empty()) {
            std::string python_exe = venv_path;
#ifdef _WIN32
            python_exe += "\\Scripts\\python.exe";
#else
            python_exe += "/bin/python";
#endif
            if (os.attr("path").attr("exists")(python_exe).cast<bool>()) {
                sys.attr("executable") = python_exe;
                sys.attr("prefix")     = venv_path;
            }
        }

        site.attr("main")();

        py::list site_packages = site.attr("getsitepackages")().cast<py::list>();
        py::list path          = sys.attr("path");
        for (size_t i = 0; i < site_packages.size(); ++i) {
            std::string pkg_path = py::str(site_packages[i]).cast<std::string>();
            bool exists          = false;
            for (size_t j = 0; j < path.size(); ++j) {
                std::string path_str = py::str(path[j]).cast<std::string>();
                if (pkg_path == path_str ||
                    os.attr("path").attr("normpath")(pkg_path).cast<std::string>() ==
                        os.attr("path").attr("normpath")(path_str).cast<std::string>()) {
                    exists = true;
                    break;
                }
            }
            if (!exists && os.attr("path").attr("exists")(pkg_path).cast<bool>())
                path.insert(0, pkg_path);
        }
    } catch (...) {
        // 失败时继续，不阻塞加载
    }
}

namespace {

const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "py_call",
            {
                {
                    "python:py_call",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(fn: string | PyObject, ...args: PyObject[]) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1)
                                return std::nullopt;
                            if (norm[0]->code() != TypeCode::String &&
                                norm[0]->code() != PyObjectType::typeCode())
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_exec",
            {
                {
                    "python:py_exec",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(code: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1 || norm[0]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_eval",
            {
                {
                    "python:py_eval",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(expr: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1 || norm[0]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_run",
            {
                {
                    "python:py_run",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(file_path: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1 || norm[0]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_attr",
            {
                {
                    "python:py_attr",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {2, {false, false}}},
                        "(obj: PyObject, attr: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 2)
                                return std::nullopt;
                            if (norm[0]->code() != PyObjectType::typeCode())
                                return std::nullopt;
                            if (norm[1]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_import",
            {
                {
                    "python:py_import",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(module_name: string) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm[0]->code() != TypeCode::String)
                                return std::nullopt;
                            return getPyObjectType();
                        }),
                },
            }),
        OperatorGroup::create(
            "py_print",
            {
                {
                    "python:py_print",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {false}}},
                        "(...objs: PyObject[]) => PyObject?",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            for (Type *t : norm)
                                if (!t->isOtherType() || t->code() != PyObjectType::typeCode())
                                    return std::nullopt;
                            return norm.empty() ? Type::Void() : norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "py_println",
            {
                {
                    "python:py_println",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {false}}},
                        "(...objs: PyObject[]) => PyObject?",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            for (Type *t : norm)
                                if (!t->isOtherType() || t->code() != PyObjectType::typeCode())
                                    return std::nullopt;
                            return norm.empty() ? Type::Void() : norm[0];
                        }),
                },
            }),
        OperatorGroup::create(
            "wrap",
            {
                {
                    "python:wrap",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(x: T) => PyObject<T>",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            return getPyObjectType()->cloneWithParams(
                                std::span<Type *const>(norm.data(), 1));
                        }),
                },
            }),
        OperatorGroup::create(
            "unwrap",
            {
                {
                    "python:unwrap",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {1, {false}}},
                        "(obj: PyObject<T>) => T",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            Type *t = norm[0];
                            if (!t->isOtherType() || t->code() != PyObjectType::typeCode())
                                return std::nullopt;
                            auto *o = static_cast<OtherType *>(t);
                            if (o->paramCount() == 0)
                                return std::nullopt;
                            return o->params()[0];
                        }),
                },
            }),
    };
    return groups;
}

} // namespace

PythonModule::PythonModule(context_ptr_t ctx) : BuiltinModule("python", ctx) {
    exportType(Reference("PyObject"), getPyObjectType());
    for (const auto &group : getOperatorGroups())
        exportEntity(group->name(), group);
}

module_ptr_t PythonModule::create(context_ptr_t ctx) { return std::make_shared<PythonModule>(ctx); }

bool PythonModule::load() {
    if (loaded_)
        return true;
    // 在模块加载时完成 Python 初始化，后续 python 协议算子无需再检查
    try {
        if (!Py_IsInitialized())
            py::initialize_interpreter();
        ensure_python_path();
    } catch (const std::exception &e) {
        context_->rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("Failed to load python module: ") + e.what());
        return false;
    }
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("python", [ctx]() { return createPythonExecutor(ctx); });
    loaded_ = true;
    return true;
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new PythonModule(ctx->shared_from_this()); }
}
