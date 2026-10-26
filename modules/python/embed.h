// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/context/context.h"
#include "camel/utils/env.h"
#include "camel/utils/once.h"
#include "camel/utils/path.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <sstream>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <dlfcn.h>
#endif

/*
 * The one embedded CPython of the process (`modules/python` and `modules/pyplot` share it).
 *
 * `import` executes no code (design principles 3.1):
 * a module's `load()` registers its executor and its operators and nothing else,
 * and the interpreter is created at the first use of an operator that needs it. Whichever module
 * is used first creates it; the other attaches and must not create a second one. The creation is
 * one sequence for both, so "who was first" does not decide how the interpreter is set up: the
 * venv is honoured, libpython is promoted to the global namespace (a C extension module imported
 * later resolves interpreter symbols from there), Python's output is line-buffered and flushed at
 * exit, and the site-packages and extra paths are in `sys.path`. `modules/pyplot` used to create
 * an interpreter with a shorter sequence and a program that plotted and then imported `_ssl`
 * failed; that is fixed here, in the one place both modules go through.
 *
 * The one-time state is in libcamel (`camel::utils::runOnce`), not in this header: the header is
 * compiled into each module's own shared object, so a function-local flag here would be one flag
 * per module and two modules whose first use runs at the same time (parallel scheduler) could
 * both create the interpreter.
 *
 * The thread that creates the interpreter holds the GIL while it finishes the sequence and gives
 * it up at the end (PyEval_SaveThread); operators of either module take the GIL for their own
 * duration, as they did when the interpreter was created by a `load()`.
 */

namespace camel::pyembed {

namespace py = pybind11;

/// Whether the embedded interpreter exists. Reading it never creates one.
inline bool isInitialized() { return Py_IsInitialized() != 0; }

namespace detail {

namespace fs = std::filesystem;

inline bool envFlagEnabled(const std::string &key) {
    std::string value = getEnv(key);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

inline std::vector<std::string> splitPathList(const std::string &value) {
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

inline PyMajorMinor compiledPythonVersion() { return PyMajorMinor{PY_MAJOR_VERSION, PY_MINOR_VERSION}; }

inline std::optional<std::string> activeVenvPath() {
    std::string venv = getEnv("VIRTUAL_ENV");
    if (!venv.empty())
        return venv;
    std::string conda = getEnv("CONDA_PREFIX");
    if (!conda.empty())
        return conda;
    return std::nullopt;
}

inline std::optional<std::string> readPyVenvCfgValue(const std::string &venvPath, const std::string &key) {
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

inline PyMajorMinor parseVersion(const std::string &text) {
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

inline bool isVenvVersionCompatible(const std::string &venvPath) {
    auto expected = compiledPythonVersion();
    auto rawVer   = readPyVenvCfgValue(venvPath, "version");
    if (!rawVer.has_value())
        return true;
    auto parsed = parseVersion(rawVer.value());
    if (!parsed.valid())
        return true;
    return parsed.major == expected.major && parsed.minor == expected.minor;
}

inline std::vector<std::string> activeEnvSitePackagePaths() {
    std::vector<std::string> out;
    std::string venv = getEnv("VIRTUAL_ENV");
    if (!venv.empty()) {
#ifdef _WIN32
        out.push_back(venv + "\\Lib\\site-packages");
#else
        out.push_back(venv + "/lib/python/site-packages");
#endif
    }
    std::string conda = getEnv("CONDA_PREFIX");
    if (!conda.empty()) {
#ifdef _WIN32
        out.push_back(conda + "\\Lib\\site-packages");
#else
        out.push_back(conda + "/lib/python/site-packages");
#endif
    }
    return out;
}

// Camel loads modules with RTLD_LOCAL, so libpython, pulled in as a module's dependency, is not
// in the global namespace. Python's C extension modules (_ssl, _ctypes, numpy, ...) resolve
// interpreter symbols such as PyExc_ValueError from there and fail to import without it, which
// breaks https in urllib among others. Promote the already loaded libpython to global.
inline void promote_libpython_to_global() {
#if !defined(_WIN32)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&Py_IsInitialized), &info) != 0 && info.dli_fname) {
        (void)dlopen(info.dli_fname, RTLD_NOW | RTLD_GLOBAL | RTLD_NOLOAD);
    }
#endif
}

// Set Python Home to the venv's base Python (from pyvenv.cfg) before Py_Initialize. Pointing it
// at the venv itself triggers codec errors; the base Python removes the "Could not find platform
// independent libraries" warning.
inline void set_python_home_from_venv() {
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

inline void prepend_path_if_missing(py::list &path, py::object &normalize, const std::string &dir) {
    if (dir.empty())
        return;
    std::error_code ec;
    fs::path absPath = fs::absolute(camel::utils::pathFromUtf8(dir), ec);
    if (ec || !fs::exists(absPath))
        return;
    std::string dirStr  = camel::utils::pathToUtf8(absPath.lexically_normal());
    std::string normDir = normalize(dirStr).cast<std::string>();
    for (size_t i = 0; i < path.size(); ++i) {
        std::string existing = py::str(path[i]).cast<std::string>();
        if (normalize(existing).cast<std::string>() == normDir)
            return;
    }
    path.attr("insert")(0, dirStr);
}

// Ensures the venv's site-packages is in sys.path after initialisation.
inline void ensure_site_packages_in_path() {
    if (!Py_IsInitialized())
        return;
    auto venv = activeVenvPath();
    if (!venv.has_value())
        return;
    if (!isVenvVersionCompatible(venv.value()))
        // Prevent hard-to-debug cross-minor contamination (e.g. SRE module mismatch).
        return;
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
        std::string norm_p = os.attr("path").attr("normpath")(site_packages_path).cast<std::string>();
        for (size_t j = 0; j < path.size(); ++j) {
            std::string path_str = py::str(path[j]).cast<std::string>();
            std::string norm_j =
                os.attr("path").attr("normpath")(path_str).cast<std::string>();
            if (norm_p == norm_j || site_packages_path == path_str)
                return;
        }
        path.attr("insert")(0, site_packages_path);
    } catch (...) {
        // Failing to add a path does not block the interpreter.
    }
}

inline void ensure_extra_python_paths_in_path() {
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
        for (const auto &dir : activeEnvSitePackagePaths()) {
            prepend_path_if_missing(path, normalize, dir);
        }
    } catch (...) {
        // Failing to add a path does not block the interpreter.
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

// Python buffers its standard output when that is not a terminal, and this interpreter is never
// finalized (objects of the program may still refer to it when the process ends), so what a
// Python `print` wrote to a pipe or a file never arrived. Two measures: standard output is put
// in line mode, so that a line is written when it is printed and stays in order with the
// program's own output; and whatever is still buffered is flushed when the process exits,
// whether the program returned, called exit() or was stopped by a fault.
inline void flushPythonStandardStreams() {
    if (!Py_IsInitialized())
        return;
    const PyGILState_STATE state = PyGILState_Ensure();
    for (const char *name : {"stdout", "stderr"}) {
        PyObject *stream = PySys_GetObject(name); // borrowed
        if (stream == nullptr || stream == Py_None)
            continue;
        if (PyObject *result = PyObject_CallMethod(stream, "flush", nullptr)) {
            Py_DECREF(result);
        } else {
            PyErr_Clear();
        }
    }
    PyGILState_Release(state);
}

inline void keepPythonOutputInOrder() {
    try {
        py::module_::import("sys").attr("stdout").attr("reconfigure")(
            py::arg("line_buffering") = true);
    } catch (...) {
        // A replaced sys.stdout without reconfigure: the flush at exit still applies.
    }
    std::atexit(flushPythonStandardStreams);
}

} // namespace detail

/// Whether the active venv (VIRTUAL_ENV / CONDA_PREFIX) was built for this binary's Python.
inline bool activeVenvMatchesBuild() {
    auto venv = detail::activeVenvPath();
    return !venv.has_value() || detail::isVenvVersionCompatible(venv.value());
}

/// The version the active venv reports, `unknown` when it reports none.
inline std::string activeVenvVersion() {
    auto venv = detail::activeVenvPath();
    if (!venv.has_value())
        return "unknown";
    return detail::readPyVenvCfgValue(venv.value(), "version").value_or("unknown");
}

/// Creates the embedded interpreter if no module of this process has, and attaches to it
/// otherwise. Throws `std::exception` when CPython cannot be initialised; the caller turns that
/// into the fault of the operator whose first use asked for it.
inline void ensureInitialized() {
    // The key is shared by every module of the process: one creation, one thread inside it.
    camel::utils::runOnce("camel.embedded-python", [] {
        if (Py_IsInitialized())
            return;
        detail::PythonInitEnvGuard envGuard;
        detail::promote_libpython_to_global();
        detail::set_python_home_from_venv();
        py::initialize_interpreter();
        detail::keepPythonOutputInOrder();
        detail::ensure_site_packages_in_path();
        detail::ensure_extra_python_paths_in_path();
        // The creating thread gives the GIL up: operators of either module take it for their own
        // duration, and a thread that never held it has no Python thread state at all.
        (void)PyEval_SaveThread();
    });
}

// Lets Python code import modules beside the entry script (e.g. test/cases/modules/std/python/).
// The interpreter can predate the script's context (another module created it), so a module syncs
// its own entry directory at its first use.
inline void ensureContextPathsInPath(camel::core::context::Context &ctx) {
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
        detail::prepend_path_if_missing(path, normalize, ctx.entryDir());
    } catch (...) {
        // Failing to add a path does not block the interpreter.
    }
}

} // namespace camel::pyembed