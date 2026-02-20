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
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type/base.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/other.h"

#include <memory>
#include <pybind11/embed.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>

namespace py = pybind11;

namespace {

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

// 递归：按类型与 slot 将 Camel 值转为 Python 对象（支持复合类型与嵌套）
py::object camelToPy(Type *t, slot_t s, Context &ctx) {
    switch (t->code()) {
    case TypeCode::Int32:
        return py::cast(static_cast<int64_t>(fromSlot<Int32>(s)));
    case TypeCode::Int64:
        return py::cast(fromSlot<Int64>(s));
    case TypeCode::Float32:
        return py::cast(fromSlot<Float32>(s));
    case TypeCode::Float64:
        return py::cast(fromSlot<Float64>(s));
    case TypeCode::Bool:
        return py::cast(fromSlot<Bool>(s));
    case TypeCode::Byte:
        return py::cast(static_cast<int>(static_cast<unsigned>(fromSlot<Byte>(s))));
    case TypeCode::String: {
        String *str = fromSlot<String *>(s);
        return str ? py::cast(str->toString()) : py::none();
    }
    case TypeCode::Array: {
        Array *arr = fromSlot<Array *>(s);
        if (!arr) return py::list();
        const ArrayType *arrayType = static_cast<const ArrayType *>(t);
        Type *elemType             = arrayType->elemType();
        py::list lst;
        for (size_t i = 0; i < arr->size(); ++i)
            lst.append(camelToPy(elemType, arr->data()[i], ctx));
        return std::move(lst);
    }
    case TypeCode::Tuple: {
        Tuple *tuple = fromSlot<Tuple *>(s);
        if (!tuple) return py::tuple();
        const TupleType *tupleType = static_cast<const TupleType *>(t);
        py::list lst;
        for (size_t i = 0; i < tupleType->size(); ++i)
            lst.append(camelToPy(tupleType->typeAt(i), tuple->data()[i], ctx));
        return py::tuple(lst);
    }
    case TypeCode::Struct: {
        Struct *st = fromSlot<Struct *>(s);
        if (!st) return py::dict();
        const StructType *structType = static_cast<const StructType *>(t);
        py::dict d;
        for (size_t i = 0; i < structType->size(); ++i) {
            py::object val = camelToPy(structType->typeAt(i), st->data()[i], ctx);
            d[py::str(std::string(structType->fieldName(i)))] = val;
        }
        return std::move(d);
    }
    default:
        if (t->code() == PyObjectType::typeCode()) {
            PythonObjectHolder *h = unwrapPyObject(s, ctx);
            return h ? py::reinterpret_borrow<py::object>(h->obj) : py::none();
        }
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python: unsupported type for to_py");
        return py::none();
    }
}

py::object camelToPy(ArgsView &norm, size_t index, Context &ctx) {
    return camelToPy(norm.type(index), norm.slot(index), ctx);
}

} // namespace

std::unordered_map<std::string, operator_t> getPythonOpsMap() {
    return {
        {"eval", __python_eval__},
        {"to_py", __python_to_py__},
        {"from_py", __python_from_py__},
    };
}

slot_t __python_eval__(ArgsView &with, ArgsView &norm, Context &ctx) {
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
    if (norm.size() < 1) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python.to_py: one argument required");
        return NullSlot;
    }
    py::object o = camelToPy(norm, 0, ctx);
    return wrapPyObject(o);
}

// 递归：将 Python 对象按目标类型转为 Camel slot（支持复合类型与嵌套）。失败返回 NullSlot。
static slot_t pyToCamel(py::object obj, Type *targetType, Context &ctx) {
    try {
        switch (targetType->code()) {
        case TypeCode::Int32:
            return toSlot(py::cast<Int32>(obj));
        case TypeCode::Int64:
            return toSlot(py::cast<Int64>(obj));
        case TypeCode::Float32:
            return toSlot(py::cast<Float32>(obj));
        case TypeCode::Float64:
            return toSlot(py::cast<Float64>(obj));
        case TypeCode::Bool:
            return toSlot(py::cast<Bool>(obj));
        case TypeCode::Byte: {
            int v = py::cast<int>(obj);
            if (v < 0 || v > 255) {
                ctx.rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit("python.from_py: byte value must be 0-255");
                return NullSlot;
            }
            return toSlot(static_cast<Byte>(static_cast<unsigned>(v)));
        }
        case TypeCode::String: {
            std::string str = py::cast<std::string>(obj);
            return toSlot(String::from(str, mm::autoSpace()));
        }
        case TypeCode::Array: {
            if (!py::isinstance<py::list>(obj)) {
                ctx.rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit("python.from_py: array target requires Python list");
                return NullSlot;
            }
            py::list lst   = obj.cast<py::list>();
            const ArrayType *arrayType = static_cast<const ArrayType *>(targetType);
            Type *elemType             = arrayType->elemType();
            size_t n                   = lst.size();
            Array *arr                 = Array::create(mm::autoSpace(), n);
            for (size_t i = 0; i < n; ++i) {
                slot_t s = pyToCamel(lst[i], elemType, ctx);
                if (s == NullSlot) return NullSlot;
                arr->set(i, s);
            }
            return toSlot(arr);
        }
        case TypeCode::Tuple: {
            if (!py::isinstance<py::tuple>(obj) && !py::isinstance<py::list>(obj)) {
                ctx.rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit("python.from_py: tuple target requires Python tuple or list");
                return NullSlot;
            }
            const TupleType *tupleType = static_cast<const TupleType *>(targetType);
            size_t n                   = tupleType->size();
            if (obj.attr("__len__")().cast<py::ssize_t>() != static_cast<py::ssize_t>(n)) {
                ctx.rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit("python.from_py: tuple length mismatch");
                return NullSlot;
            }
            Tuple *tuple = Tuple::create(n, mm::autoSpace());
            for (size_t i = 0; i < n; ++i) {
                slot_t s = pyToCamel(obj[py::int_(i)], tupleType->typeAt(i), ctx);
                if (s == NullSlot) return NullSlot;
                tuple->set(i, s);
            }
            return toSlot(tuple);
        }
        case TypeCode::Struct: {
            if (!py::isinstance<py::dict>(obj)) {
                ctx.rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit("python.from_py: struct target requires Python dict");
                return NullSlot;
            }
            py::dict d            = obj.cast<py::dict>();
            const StructType *structType = static_cast<const StructType *>(targetType);
            size_t n             = structType->size();
            Struct *st           = Struct::create(n, mm::autoSpace());
            for (size_t i = 0; i < n; ++i) {
                std::string key = std::string(structType->fieldName(i));
                py::str keyObj(key);
                if (!d.contains(keyObj)) {
                    ctx.rtmDiags()
                        ->of(RuntimeDiag::RuntimeError)
                        .commit("python.from_py: struct missing field '" + key + "'");
                    return NullSlot;
                }
                slot_t s = pyToCamel(d[keyObj], structType->typeAt(i), ctx);
                if (s == NullSlot) return NullSlot;
                st->set(i, s);
            }
            return toSlot(st);
        }
        default:
            if (targetType->code() == getPyObjectType()->code())
                return wrapPyObject(obj);
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("python.from_py: unsupported target type " + targetType->toString());
            return NullSlot;
        }
    } catch (...) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.from_py: conversion failed");
        return NullSlot;
    }
}

slot_t __python_from_py__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.size() < 1)
        return NullSlot;
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    if (!h)
        return NullSlot;
    Type *argType = norm.type(0);
    if (!argType->isOtherType() || argType->code() != getPyObjectType()->code()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.from_py: argument must be PyObject<T> with type param");
        return NullSlot;
    }
    auto *o = static_cast<OtherType *>(argType);
    if (o->paramCount() == 0 || !o->params()[0]) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.from_py: PyObject must have one type parameter");
        return NullSlot;
    }
    Type *targetType = o->params()[0];
    return pyToCamel(h->obj, targetType, ctx);
}
