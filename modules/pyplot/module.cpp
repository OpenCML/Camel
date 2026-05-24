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
 * Updated: May. 24, 2026
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
#include "executor.h"
#include "operators.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <string>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace py = pybind11;
namespace fs = std::filesystem;

namespace {

bool envFlagEnabled(const std::string &key) {
    std::string value = getEnv(key);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

std::vector<std::string> splitPathList(const std::string &value) {
    std::vector<std::string> out;
    if (value.empty())
        return out;
#ifdef _WIN32
    constexpr char delim = ';';
#else
    constexpr char delim = ':';
#endif
    std::stringstream ss(value);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty())
            out.push_back(item);
    }
    return out;
}

struct PyMajorMinor {
    int major = -1;
    int minor = -1;
    bool valid() const { return major >= 0 && minor >= 0; }
};

PyMajorMinor compiledPythonVersion() { return PyMajorMinor{PY_MAJOR_VERSION, PY_MINOR_VERSION}; }

std::optional<std::string> activeVenvPath() {
    std::string venv = getEnv("VIRTUAL_ENV");
    if (!venv.empty())
        return venv;
    std::string conda = getEnv("CONDA_PREFIX");
    if (!conda.empty())
        return conda;
    return std::nullopt;
}

std::optional<std::string> readPyVenvCfgValue(const std::string &venvPath, const std::string &key) {
    if (venvPath.empty())
        return std::nullopt;
#ifdef _WIN32
    std::string cfgPath = venvPath + "\\pyvenv.cfg";
#else
    std::string cfgPath = venvPath + "/pyvenv.cfg";
#endif
    std::ifstream f(cfgPath);
    if (!f)
        return std::nullopt;
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind(key, 0) != 0)
            continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        std::string value = line.substr(eq + 1);
        size_t start      = value.find_first_not_of(" \t");
        if (start != std::string::npos)
            value = value.substr(start);
        size_t end = value.find_last_not_of(" \t");
        if (end != std::string::npos)
            value = value.substr(0, end + 1);
        if (!value.empty())
            return value;
    }
    return std::nullopt;
}

PyMajorMinor parseVersion(const std::string &text) {
    PyMajorMinor out;
    if (text.empty())
        return out;
    char dot = 0;
    std::stringstream ss(text);
    if ((ss >> out.major) && (ss >> dot) && dot == '.' && (ss >> out.minor))
        return out;
    out.major = -1;
    out.minor = -1;
    return out;
}

bool isVenvVersionCompatible(const std::string &venvPath) {
    auto expected = compiledPythonVersion();
    auto rawVer   = readPyVenvCfgValue(venvPath, "version");
    if (!rawVer.has_value())
        return true;
    auto parsed = parseVersion(rawVer.value());
    if (!parsed.valid())
        return true;
    return parsed.major == expected.major && parsed.minor == expected.minor;
}

void prepend_path_if_missing(py::list &path, py::object &normalize, const std::string &dir) {
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

void ensure_site_packages_in_path() {
    if (!Py_IsInitialized())
        return;
    auto envRoot = activeVenvPath();
    if (!envRoot.has_value())
        return;
    if (!isVenvVersionCompatible(envRoot.value()))
        return;
    std::string sitePackages;
#ifdef _WIN32
    sitePackages = envRoot.value() + "\\Lib\\site-packages";
#else
    sitePackages = envRoot.value() + "/lib/python";
    try {
        py::module_ sys = py::module_::import("sys");
        std::string ver = py::str(sys.attr("version_info").attr("major")).cast<std::string>() +
                          "." + py::str(sys.attr("version_info").attr("minor")).cast<std::string>();
        sitePackages += ver + "/site-packages";
    } catch (...) {
        sitePackages += "3.11/site-packages";
    }
#endif
    try {
        py::module_ sys      = py::module_::import("sys");
        py::module_ os       = py::module_::import("os");
        py::object normalize = py::cpp_function([&os](const std::string &value) {
            py::object pathMod = os.attr("path");
            return pathMod.attr("normcase")(pathMod.attr("normpath")(value));
        });
        py::list path        = sys.attr("path");
        prepend_path_if_missing(path, normalize, sitePackages);
    } catch (...) {
        // 失败时继续，不阻塞加载
    }
}

void ensure_extra_python_paths_in_path() {
    if (!Py_IsInitialized())
        return;
    try {
        py::module_ sys      = py::module_::import("sys");
        py::module_ os       = py::module_::import("os");
        py::object normalize = py::cpp_function([&os](const std::string &value) {
            py::object pathMod = os.attr("path");
            return pathMod.attr("normcase")(pathMod.attr("normpath")(value));
        });
        py::list path        = sys.attr("path");
        for (const auto &dir : splitPathList(getEnv("CAMEL_PYTHONPATH"))) {
            prepend_path_if_missing(path, normalize, dir);
        }
        if (envFlagEnabled("CAMEL_PYTHON_INHERIT_HOST_PYTHONPATH")) {
            for (const auto &dir : splitPathList(getEnv("PYTHONPATH"))) {
                prepend_path_if_missing(path, normalize, dir);
            }
        }
    } catch (...) {
        // 失败时继续，不阻塞加载
    }
}

struct PythonInitEnvGuard {
    std::optional<ScopedEnvVar> pythonHome;
    std::optional<ScopedEnvVar> pythonPath;

    PythonInitEnvGuard() {
        if (envFlagEnabled("CAMEL_PYTHON_INHERIT_HOST_ENV"))
            return;
        pythonHome.emplace("PYTHONHOME", std::nullopt);
        if (!envFlagEnabled("CAMEL_PYTHON_INHERIT_HOST_PYTHONPATH"))
            pythonPath.emplace("PYTHONPATH", std::nullopt);
    }
};

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
            PythonInitEnvGuard envGuard;
            set_python_home_from_venv();
            py::initialize_interpreter();
            ensure_site_packages_in_path();
            ensure_extra_python_paths_in_path();
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
