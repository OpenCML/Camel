// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "module.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/other.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/env.h"
#include "camel/utils/log.h"
#include "embed.h"
#include "ensure.h"
#include "executor.h"
#include "operators.h"

#include <exception>
#include <initializer_list>
#include <mutex>
#include <string>
#include <vector>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

namespace py = pybind11;

namespace {

// The venv check the module has always made before creating the interpreter. It is made at the
// module's first use now, not at `import` (design principles 3.1), so a program that imports `python` and never calls one of its
// operators does not read the environment at all.
void checkActiveVenvVersion() {
    if (camel::pyembed::activeVenvMatchesBuild())
        return;
    throwRuntimeFault(
        RuntimeDiag::RuntimeError,
        "python module version mismatch (checked at the first use of python, not by import): "
        "current build binds Python " + std::to_string(PY_MAJOR_VERSION) + "." +
            std::to_string(PY_MINOR_VERSION) + ", but active environment reports " +
            camel::pyembed::activeVenvVersion() +
            ". Use a matching venv, or provide routed multi-version bridges.");
}

} // namespace

namespace {

// The operators whose signature C++ computes stay here (design principles 6.4).
// `py_call` (two URIs), `py_print` and `py_println` are variadic, and
// `py_wrap`/`py_unwrap` move a type through the module type `PyObject<T>`; neither can be
// written as a body-less declaration. The other six are declared in stdlib/python.cml.
const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "py_call",
            {
                {
                    "python:py_call",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(fn: string | PyObject, ...args: PyObject[]) => PyObject!",
                        [](const type_vec_t &with, const type_vec_t &norm)
                            -> std::optional<Type *> {
                            if (norm.size() < 1)
                                return std::nullopt;
                            if (norm[0]->code() != TypeCode::String &&
                                norm[0]->code() != PyObjectType::typeCode())
                                return std::nullopt;
                            for (size_t i = 1; i < norm.size(); ++i)
                                if (!norm[i] || norm[i]->code() != PyObjectType::typeCode())
                                    return std::nullopt;
                            return TupleType::fallible(getPyObjectType());
                        }),
                },
                {
                    "python:py_call_kw",
                    DynamicFuncTypeResolver::create(
                        {{0, {}}, {-1, {}}},
                        "(fn: string | PyObject, args?: (...PyObject) | PyObject[], kwargs?: "
                        "Struct<...PyObject>) => PyObject!",
                        [](const type_vec_t &with, const type_vec_t &norm)
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
                            return TupleType::fallible(getPyObjectType());
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
                        [](const type_vec_t &with, const type_vec_t &norm)
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
                        [](const type_vec_t &with, const type_vec_t &norm)
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
                        [](const type_vec_t &with, const type_vec_t &norm)
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
                        "(obj: PyObject<T>) => T!",
                        [](const type_vec_t &with, const type_vec_t &norm)
                            -> std::optional<Type *> {
                            Type *t = norm[0];
                            if (!t->isOtherType() || t->code() != PyObjectType::typeCode())
                                return std::nullopt;
                            auto *o = static_cast<OtherType *>(t);
                            if (o->paramCount() == 0)
                                return std::nullopt;
                            return TupleType::fallible(o->params()[0]);
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

// What interpreter is running and which environment it came from: the INFO lines `load()` used to
// write, at the first use, where the rest of the interpreter's story now happens.
static void logInterpreterMetadata() {
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
}

bool PythonModule::load() {
    if (loaded_)
        return true;
    // `import` executes no code (design principles 3.1): loading registers the executor and the purity of the operators,
    // which the declaration loader needs, and nothing that touches CPython. The interpreter is
    // created by the first operator call (`ensurePythonInterpreter`), or attached to when
    // `pyplot` created it first.
    context_ptr_t ctx = context_;
    context_->registerExecutorFactory("python", [ctx]() { return createPythonExecutor(ctx); });
    // The purity of every operator of the module (design principles 3.3), which the declaration
    // loader requires before it accepts a body-less declaration (`Builder::declareNativeOperator`,
    // src/compile/gir/builder_modules.cpp#declareNativeOperator). All twelve are effectful:
    // each one observes or advances the interpreter, which no argument names. `py_call`, `py_exec`,
    // `py_eval` and `py_run` execute arbitrary Python; `py_attr` may run a descriptor or a
    // property; `py_import` runs a module body and writes `sys.modules`; `py_print`/`py_println`
    // write the interpreter's standard output; `py_wrap` creates an object whose identity and
    // reference count are interpreter state, `py_unwrap` reads an object that Python code can
    // have changed, and `py_dispose` drops a reference. None is state-only: their state is the
    // interpreter, not a variable of the program.
    camel::core::OperatorTable::instance().declare(
        "python",
        {{"py_call", false},
         {"py_call_kw", false},
         {"py_exec", false},
         {"py_eval", false},
         {"py_run", false},
         {"py_attr", false},
         {"py_import", false},
         {"py_print", false},
         {"py_println", false},
         {"py_wrap", false},
         {"py_unwrap", false},
         {"py_dispose", false}});
    loaded_ = true;
    return true;
}

void ensurePythonInterpreter(camel::core::context::Context &ctx) {
    // Once for the module object: the environment check, the creation (or the attachment) and the
    // entry directory. The creation itself is guarded across modules by
    // `camel::pyembed::ensureInitialized`, so `pyplot` and this module cannot both create one.
    static std::once_flag firstUse;
    std::call_once(firstUse, [&ctx] {
        checkActiveVenvVersion();
        try {
            camel::pyembed::ensureInitialized();
        } catch (const std::exception &e) {
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                std::string("Failed to initialise python's embedded Python interpreter at its "
                            "first use: ") +
                    e.what());
        }
        // The interpreter exists here (created now, or by `pyplot` earlier) and the GIL is free:
        // take it for the path sync and the metadata line, and give it back at the end.
        py::gil_scoped_acquire gil;
        camel::pyembed::ensureContextPathsInPath(ctx);
        logInterpreterMetadata();
    });
}

extern "C" {

Module *camel_module_create(Context *ctx) { return new PythonModule(ctx->shared_from_this()); }
}
