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
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/other.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/env.h"
#include "camel/utils/log.h"
#include "executor.h"
#include "operators.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <sstream>
#include <string>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace py = pybind11;
namespace fs = std::filesystem;

namespace {

struct PyMajorMinor {
    int major = -1;
    int minor = -1;
    bool valid() const { return major >= 0 && minor >= 0; }
};

PyMajorMinor compiledPythonVersion() { return PyMajorMinor{PY_MAJOR_VERSION, PY_MINOR_VERSION}; }

std::optional<std::string> readPyVenvCfgValue(const std::string &venvPath, const std::string &key) {
    if (venvPath.empty()) {
        return std::nullopt;
    }
#ifdef _WIN32
    std::string cfgPath = venvPath + "\\pyvenv.cfg";
#else
    std::string cfgPath = venvPath + "/pyvenv.cfg";
#endif
    std::ifstream f(cfgPath);
    if (!f) {
        return std::nullopt;
    }
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) != 0) {
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string value = line.substr(eq + 1);
        size_t start      = value.find_first_not_of(" \t");
        if (start != std::string::npos) {
            value = value.substr(start);
        }
        size_t end = value.find_last_not_of(" \t");
        if (end != std::string::npos) {
            value = value.substr(0, end + 1);
        }
        if (!value.empty()) {
            return value;
        }
    }
    return std::nullopt;
}

PyMajorMinor parseVersion(const std::string &text) {
    PyMajorMinor out;
    if (text.empty()) {
        return out;
    }
    char dot = 0;
    std::stringstream ss(text);
    if ((ss >> out.major) && (ss >> dot) && dot == '.' && (ss >> out.minor)) {
        return out;
    }
    out.major = -1;
    out.minor = -1;
    return out;
}

std::optional<std::string> activeVenvPath() {
    std::string venv = getEnv("VIRTUAL_ENV");
    if (!venv.empty()) {
        return venv;
    }
    std::string conda = getEnv("CONDA_PREFIX");
    if (!conda.empty()) {
        return conda;
    }
    return std::nullopt;
}

bool isVenvVersionCompatible(const std::string &venvPath) {
    auto expected = compiledPythonVersion();
    auto rawVer   = readPyVenvCfgValue(venvPath, "version");
    if (!rawVer.has_value()) {
        // Cannot prove mismatch; keep previous behavior.
        return true;
    }
    auto parsed = parseVersion(rawVer.value());
    if (!parsed.valid()) {
        return true;
    }
    return parsed.major == expected.major && parsed.minor == expected.minor;
}

} // namespace

// 在 Py_Initialize 之前设置 Python Home 为 venv 的 base Python（来自 pyvenv.cfg）。
// 指向 venv 本身会触发 codec 错误，指向 base Python 可消除 "Could not find platform independent
// libraries" 警告。
static void set_python_home_from_venv() {
    auto venv = activeVenvPath();
    if (!venv.has_value())
        return;
    if (!isVenvVersionCompatible(venv.value()))
        return;
    auto home = readPyVenvCfgValue(venv.value(), "home");
    if (!home.has_value())
        return;
    wchar_t *whome = Py_DecodeLocale(home.value().c_str(), nullptr);
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

// 初始化后确保 venv 的 site-packages 在 sys.path 中。
static void ensure_site_packages_in_path() {
    if (!Py_IsInitialized())
        return;
    auto venv = activeVenvPath();
    if (!venv.has_value())
        return;
    if (!isVenvVersionCompatible(venv.value())) {
        // Prevent hard-to-debug cross-minor contamination (e.g. SRE module mismatch).
        return;
    }
    const std::string &venv_path = venv.value();
    std::string site_packages_path;
#ifdef _WIN32
    site_packages_path = venv_path + "\\Lib\\site-packages";
#else
    site_packages_path = venv_path + "/lib/python";
    try {
        py::module_ sys = py::module_::import("sys");
        std::string ver = py::str(sys.attr("version_info").attr("major")).cast<std::string>() +
                          "." + py::str(sys.attr("version_info").attr("minor")).cast<std::string>();
        site_packages_path += ver + "/site-packages";
    } catch (...) {
        site_packages_path += "3.11/site-packages"; // fallback
    }
#endif
    try {
        py::module_ sys = py::module_::import("sys");
        py::module_ os  = py::module_::import("os");
        py::list path   = sys.attr("path");
        if (!os.attr("path").attr("exists")(site_packages_path).cast<bool>())
            return;
        for (size_t j = 0; j < path.size(); ++j) {
            std::string path_str = py::str(path[j]).cast<std::string>();
            std::string norm_p =
                os.attr("path").attr("normpath")(site_packages_path).cast<std::string>();
            std::string norm_j = os.attr("path").attr("normpath")(path_str).cast<std::string>();
            if (norm_p == norm_j || site_packages_path == path_str)
                return;
        }
        path.attr("insert")(0, site_packages_path);
    } catch (...) {
        // 失败时继续，不阻塞加载
    }
}

// 将候选目录安全地插到 sys.path 前面，避免重复插入同一路径。
static void prepend_path_if_missing(py::list &path, py::object &normalize, const std::string &dir) {
    if (dir.empty())
        return;
    std::error_code ec;
    fs::path absPath = fs::absolute(fs::path(dir), ec);
    if (ec || !fs::exists(absPath))
        return;
    std::string dirStr  = absPath.lexically_normal().string();
    std::string normDir = normalize(dirStr).cast<std::string>();
    for (size_t i = 0; i < path.size(); ++i) {
        std::string existing = py::str(path[i]).cast<std::string>();
        if (normalize(existing).cast<std::string>() == normDir)
            return;
    }
    path.attr("insert")(0, dirStr);
}

// 让 Python 侧能直接 import 入口脚本同目录下的辅助模块，例如 test/run/nn/mnist_loader.py。
// 这里只同步 Camel 上下文的入口目录；site-packages 仍由上面的 venv 逻辑负责。
static void ensure_context_paths_in_path(const context_ptr_t &ctx) {
    if (!Py_IsInitialized() || !ctx)
        return;
    try {
        py::module_ sys      = py::module_::import("sys");
        py::module_ os       = py::module_::import("os");
        py::object normalize = py::cpp_function([&os](const std::string &value) {
            py::object pathMod = os.attr("path");
            return pathMod.attr("normcase")(pathMod.attr("normpath")(value));
        });
        py::list path        = sys.attr("path");
        prepend_path_if_missing(path, normalize, ctx->entryDir());
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
                            for (size_t i = 1; i < norm.size(); ++i)
                                if (!norm[i] || norm[i]->code() != PyObjectType::typeCode())
                                    return std::nullopt;
                            return getPyObjectType();
                        }),
                },
                {
                    "python:py_call_kw",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(fn: string | PyObject, args?: (...PyObject) | PyObject[], kwargs?: "
                        "Struct<...PyObject>) => PyObject",
                        [](const type_vec_t &with, const type_vec_t &norm, const ModifierSet &)
                            -> std::optional<Type *> {
                            if (norm.size() < 1 || norm.size() > 3)
                                return std::nullopt;
                            if (norm[0]->code() != TypeCode::String &&
                                norm[0]->code() != PyObjectType::typeCode())
                                return std::nullopt;
                            if (norm.size() >= 2) {
                                if (norm[1]->code() == TypeCode::Array) {
                                    auto *arr = static_cast<ArrayType *>(norm[1]);
                                    if (!arr->elemType() ||
                                        arr->elemType()->code() != PyObjectType::typeCode())
                                        return std::nullopt;
                                } else if (norm[1]->code() == TypeCode::Tuple) {
                                    auto *tup = static_cast<TupleType *>(norm[1]);
                                    for (size_t i = 0; i < tup->size(); ++i)
                                        if (!tup->typeAt(i) ||
                                            tup->typeAt(i)->code() != PyObjectType::typeCode())
                                            return std::nullopt;
                                } else {
                                    return std::nullopt;
                                }
                            }
                            if (norm.size() == 3) {
                                if (norm[2]->code() != TypeCode::Struct)
                                    return std::nullopt;
                                auto *st = static_cast<StructType *>(norm[2]);
                                for (size_t i = 0; i < st->size(); ++i)
                                    if (!st->typeAt(i) ||
                                        st->typeAt(i)->code() != PyObjectType::typeCode())
                                        return std::nullopt;
                            }
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
            "py_wrap",
            {
                {
                    "python:py_wrap",
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
            "py_unwrap",
            {
                {
                    "python:py_unwrap",
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
        if (auto venv = activeVenvPath();
            venv.has_value() && !isVenvVersionCompatible(venv.value())) {
            auto expected   = compiledPythonVersion();
            std::string got = readPyVenvCfgValue(venv.value(), "version").value_or("unknown");
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                "python module version mismatch: current build binds Python " +
                    std::to_string(expected.major) + "." + std::to_string(expected.minor) +
                    ", but active environment reports " + got +
                    ". Use a matching venv, or provide routed multi-version bridges.");
        }
        if (!Py_IsInitialized()) {
            set_python_home_from_venv();
            py::initialize_interpreter();
            ensure_site_packages_in_path();
        }
        // Python 解释器可能早于当前脚本上下文初始化，因此每次加载模块时都重新同步入口目录。
        ensure_context_paths_in_path(context_);
        try {
            py::module_ sys     = py::module_::import("sys");
            std::string version = py::str(sys.attr("version")).cast<std::string>();
            std::string exe     = py::str(sys.attr("executable")).cast<std::string>();
            std::string prefix  = py::str(sys.attr("prefix")).cast<std::string>();
            std::string venv    = getEnv("VIRTUAL_ENV");
            std::string conda   = getEnv("CONDA_PREFIX");
            std::string envTag  = "none";
            std::string envPath = "-";
            if (!venv.empty()) {
                envTag  = "venv";
                envPath = venv;
            } else if (!conda.empty()) {
                envTag  = "conda";
                envPath = conda;
            }
            std::string verLine = version;
            if (const auto nl = verLine.find('\n'); nl != std::string::npos) {
                verLine.resize(nl);
            }
            CAMEL_LOG_INFO_S("PythonModule", "run | python | {}", verLine);
            CAMEL_LOG_INFO_S("PythonModule", "run | python | executable {}", exe);
            CAMEL_LOG_INFO_S(
                "PythonModule",
                "run | python | prefix {} | env={} | {}",
                prefix,
                envTag,
                envPath);
        } catch (...) {
            CAMEL_LOG_WARN_S("PythonModule", "run | python | could not read sys metadata");
        }
    } catch (const std::exception &e) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("Failed to load python module: ") + e.what());
    }
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("python", [ctx]() { return createPythonExecutor(ctx); });
    loaded_ = true;
    return true;
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new PythonModule(ctx->shared_from_this()); }
}
