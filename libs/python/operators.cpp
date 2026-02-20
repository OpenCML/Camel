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

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/base.h"
#include "camel/core/type/other.h"
#include "camel/utils/type.h"

#include <memory>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>


namespace py = pybind11;

namespace {

class PyObjectType : public OtherType {
  public:
    explicit PyObjectType(TypeCode code) : OtherType(code) {}

    std::string toString() const override { return typeName(); }
    std::string mangle() const override { return "PyObject"; }
    Type *clone(bool deep = false) const override { return const_cast<PyObjectType *>(this); }
    bool equals(Type *type) const override { return type && type->code() == code_; }
    CastSafety castSafetyTo(const Type &other) const override {
        if (other.code() == code_)
            return CastSafety::Safe;
        return CastSafety::Forbidden;
    }
    bool assignable(Type *type) const override { return type && type->code() == code_; }
};

Type *getPyObjectTypeImpl() {
    static TypeCode code = OtherTypeRegistry::registerType("PyObject", TypeFlag::GC_Traced);
    static PyObjectType singleton(code);
    return &singleton;
}

void ensurePython(Context &ctx) {
    if (!Py_IsInitialized()) {
        try {
            py::initialize_interpreter();
        } catch (const std::exception &e) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit(std::string("Failed to initialize Python: ") + e.what());
            throw;
        }
    }
}

struct PythonObjectHolder {
    py::object obj;
};
static std::vector<std::unique_ptr<PythonObjectHolder>> s_holders;

slot_t wrapPyObject(py::object o) {
    auto h = std::make_unique<PythonObjectHolder>(PythonObjectHolder{std::move(o)});
    PythonObjectHolder *raw = h.get();
    s_holders.push_back(std::move(h));
    return toSlot(raw);
}

PythonObjectHolder *unwrapPyObject(slot_t s, Context &ctx) {
    if (s == NullSlot) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python: expected non-null PyObject");
        return nullptr;
    }
    return reinterpret_cast<PythonObjectHolder *>(static_cast<uintptr_t>(s));
}

py::object camelToPy(ArgsView &norm, size_t index, Context &ctx) {
    Type *t  = norm.type(index);
    slot_t s = norm.slot(index);
    switch (t->code()) {
    case TypeCode::Int32:
        return py::cast(static_cast<int64_t>(norm.get<Int32>(index)));
    case TypeCode::Int64:
        return py::cast(norm.get<Int64>(index));
    case TypeCode::Float32:
        return py::cast(norm.get<Float32>(index));
    case TypeCode::Float64:
        return py::cast(norm.get<Float64>(index));
    case TypeCode::Bool:
        return py::cast(norm.get<Bool>(index));
    case TypeCode::String: {
        String *str = norm.get<String *>(index);
        return str ? py::cast(str->toString()) : py::none();
    }
    default:
        if (t->equals(getPyObjectTypeImpl())) {
            PythonObjectHolder *h = unwrapPyObject(s, ctx);
            return h ? py::reinterpret_borrow<py::object>(h->obj) : py::none();
        }
        if (isOtherType(t->code())) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit(
                    "python: Other type conversion to Python not registered (see "
                    "OtherTypeRegistry)");
            return py::none();
        }
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python: unsupported type for to_py");
        return py::none();
    }
}

} // namespace

Type *getPyObjectType() { return getPyObjectTypeImpl(); }

std::unordered_map<std::string, operator_t> getPythonOpsMap() {
    return {
        {"eval", __python_eval__},
        {"to_py", __python_to_py__},
        {"from_py_int", __python_from_py_int__},
        {"from_py_float", __python_from_py_float__},
        {"from_py_bool", __python_from_py_bool__},
        {"from_py_string", __python_from_py_string__},
    };
}

slot_t __python_eval__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ensurePython(ctx);
    if (norm.size() < 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.eval: at least one argument (function path string) required");
        return NullSlot;
    }
    if (norm.type(0)->code() != TypeCode::String) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.eval: first argument must be string (e.g. \"mymod.myfunc\")");
        return NullSlot;
    }
    String *pathObj  = norm.get<String *>(0);
    std::string path = pathObj ? pathObj->toString() : "";
    if (path.empty()) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python.eval: empty function path");
        return NullSlot;
    }

    try {
        std::vector<std::string> parts;
        for (size_t i = 0; i < path.size();) {
            size_t j = path.find('.', i);
            if (j == std::string::npos) {
                parts.push_back(path.substr(i));
                break;
            }
            parts.push_back(path.substr(i, j - i));
            i = j + 1;
        }
        if (parts.empty()) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("python.eval: invalid function path");
            return NullSlot;
        }
        py::object obj = py::module_::import(parts[0].c_str());
        for (size_t k = 1; k < parts.size(); ++k)
            obj = obj.attr(parts[k].c_str());

        py::tuple argTuple(norm.size() > 1 ? static_cast<size_t>(norm.size() - 1) : 0);
        for (size_t i = 1; i < norm.size(); ++i)
            argTuple[i - 1] = camelToPy(norm, i, ctx);

        py::object result = obj(*argTuple);
        return wrapPyObject(result);
    } catch (const py::error_already_set &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.eval: ") + e.what());
        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.eval: ") + e.what());
        return NullSlot;
    }
}

slot_t __python_to_py__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ensurePython(ctx);
    if (norm.size() < 1) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python.to_py: one argument required");
        return NullSlot;
    }
    py::object o = camelToPy(norm, 0, ctx);
    return wrapPyObject(o);
}

slot_t __python_from_py_int__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ensurePython(ctx);
    if (norm.size() < 1)
        return NullSlot;
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    if (!h)
        return NullSlot;
    try {
        Int64 v = py::cast<Int64>(h->obj);
        return toSlot(v);
    } catch (...) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.from_py_int: conversion failed");
        return NullSlot;
    }
}

slot_t __python_from_py_float__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ensurePython(ctx);
    if (norm.size() < 1)
        return NullSlot;
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    if (!h)
        return NullSlot;
    try {
        Float64 v = py::cast<Float64>(h->obj);
        return toSlot(v);
    } catch (...) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.from_py_float: conversion failed");
        return NullSlot;
    }
}

slot_t __python_from_py_bool__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ensurePython(ctx);
    if (norm.size() < 1)
        return NullSlot;
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    if (!h)
        return NullSlot;
    try {
        Bool v = py::cast<Bool>(h->obj);
        return toSlot(v);
    } catch (...) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.from_py_bool: conversion failed");
        return NullSlot;
    }
}

slot_t __python_from_py_string__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ensurePython(ctx);
    if (norm.size() < 1)
        return NullSlot;
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    if (!h)
        return NullSlot;
    try {
        std::string str = py::cast<std::string>(h->obj);
        String *s       = String::from(str, mm::autoSpace());
        return toSlot(s);
    } catch (...) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.from_py_string: conversion failed");
        return NullSlot;
    }
}
