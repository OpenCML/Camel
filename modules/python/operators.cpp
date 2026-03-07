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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/other.h"

#include <memory>
#include <optional>
#include <pybind11/embed.h>
#include <pybind11/eval.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <string>
#include <vector>

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::type;
using namespace camel::core::rtdata;

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

PythonObjectHolder *unwrapPyObject(slot_t s, camel::core::context::Context &ctx) {
    if (s == NullSlot) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python: expected non-null PyObject");
    }
    return reinterpret_cast<PythonObjectHolder *>(static_cast<uintptr_t>(s));
}

// 递归：按类型与 slot 将 Camel 值转为 Python 对象（支持复合类型与嵌套）
py::object camelToPy(camel::core::type::Type *t, slot_t s, camel::core::context::Context &ctx) {
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
    case TypeCode::Void:
        return py::none();
    case TypeCode::String: {
        String *str = fromSlot<String *>(s);
        return str ? py::cast(str->toString()) : py::none();
    }
    case TypeCode::Array: {
        Array *arr = fromSlot<Array *>(s);
        if (!arr)
            return py::list();
        const ArrayType *arrayType = static_cast<const ArrayType *>(t);
        Type *elemType             = arrayType->elemType();
        py::list lst;
        for (size_t i = 0; i < arr->size(); ++i)
            lst.append(camelToPy(elemType, arr->data()[i], ctx));
        return std::move(lst);
    }
    case TypeCode::Tuple: {
        Tuple *tuple = fromSlot<Tuple *>(s);
        if (!tuple)
            return py::tuple();
        const TupleType *tupleType = static_cast<const TupleType *>(t);
        py::list lst;
        for (size_t i = 0; i < tupleType->size(); ++i)
            lst.append(camelToPy(tupleType->typeAt(i), tuple->data()[i], ctx));
        return py::tuple(lst);
    }
    case TypeCode::Struct: {
        Struct *st = fromSlot<Struct *>(s);
        if (!st)
            return py::dict();
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
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python: unsupported type for wrap");
    }
}

py::object camelToPy(ArgsView &norm, size_t index, camel::core::context::Context &ctx) {
    return camelToPy(norm.type(index), norm.slot(index), ctx);
}

// 解析 norm[0] 为可调用对象（PyObject 或字符串路径），失败时返回 nullptr 并报告诊断
py::object resolveCallable(ArgsView &norm, camel::core::context::Context &ctx) {
    if (norm.size() < 1) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python: at least one argument (callable or path string) required");
    }
    if (norm.type(0)->code() == PyObjectType::typeCode()) {
        PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
        return h ? py::reinterpret_borrow<py::object>(h->obj) : py::none();
    }
    if (norm.type(0)->code() == TypeCode::String) {
        String *pathObj  = norm.get<String *>(0);
        std::string path = pathObj ? pathObj->toString() : "";
        if (path.empty()) {
            throwRuntimeFault(RuntimeDiag::RuntimeError, "python: empty path");
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
                throwRuntimeFault(RuntimeDiag::RuntimeError, "python: invalid path");
            }
            if (parts.size() == 1) {
                py::object main = py::module_::import("__main__");
                if (py::hasattr(main, parts[0].c_str()))
                    return main.attr(parts[0].c_str());
                return py::module_::import("builtins").attr(parts[0].c_str());
            }
            py::object obj = py::module_::import(parts[0].c_str());
            for (size_t k = 1; k < parts.size(); ++k)
                obj = obj.attr(parts[k].c_str());
            return obj;
        } catch (const py::error_already_set &e) {
            throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python: \n") + e.what());
        }
    }
    throwRuntimeFault(
        RuntimeDiag::RuntimeError,
        "python: first argument must be PyObject or string");
}

} // namespace

std::unordered_map<std::string, operator_t> getPythonOpsMap() {
    return {
        {"py_call", __python_py_call__},
        {"py_call_kw", __python_py_call_kw__},
        {"py_exec", __python_py_exec__},
        {"py_eval", __python_py_eval__},
        {"py_run", __python_py_run__},
        {"py_attr", __python_py_attr__},
        {"py_import", __python_py_import__},
        {"py_print", __python_py_print__},
        {"py_println", __python_py_println__},
        {"py_wrap", __python_wrap__},
        {"py_unwrap", __python_upwrap__},
    };
}

// py_call: 调用 Python 函数 (fn, ...args)，首参为字符串时按 "module.attr" 解析
slot_t __python_py_call__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    py::object callable = resolveCallable(norm, ctx);
    if (callable.is_none())
        return NullSlot;

    try {
        py::tuple argTuple(norm.size() > 1 ? static_cast<size_t>(norm.size() - 1) : 0);
        for (size_t i = 1; i < norm.size(); ++i)
            argTuple[i - 1] = camelToPy(norm, i, ctx);

        py::object result = callable(*argTuple);
        return wrapPyObject(result);
    } catch (const py::error_already_set &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_call: \n") + e.what());
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_call: \n") + e.what());
    }
}

// py_call_kw: 调用 Python 函数 (fn, args?: Tuple|Array<PyObject>, kwargs?: Struct<PyObject>)，1–3
// 参
slot_t __python_py_call_kw__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    py::object callable = resolveCallable(norm, ctx);
    if (callable.is_none())
        return NullSlot;

    try {
        py::tuple args;
        if (norm.size() >= 2 && norm.type(1)->code() == TypeCode::Array) {
            Array *arr = norm.get<Array *>(1);
            if (arr) {
                args = py::tuple(arr->size());
                for (size_t i = 0; i < arr->size(); ++i) {
                    PythonObjectHolder *h = unwrapPyObject(arr->data()[i], ctx);
                    if (!h)
                        return NullSlot;
                    args[i] = py::reinterpret_borrow<py::object>(h->obj);
                }
            }
        } else if (norm.size() >= 2 && norm.type(1)->code() == TypeCode::Tuple) {
            Tuple *tuple = norm.get<Tuple *>(1);
            if (tuple) {
                const TupleType *tupleType = static_cast<const TupleType *>(norm.type(1));
                args                       = py::tuple(tupleType->size());
                for (size_t i = 0; i < tupleType->size(); ++i) {
                    PythonObjectHolder *h = unwrapPyObject(tuple->data()[i], ctx);
                    if (!h)
                        return NullSlot;
                    args[i] = py::reinterpret_borrow<py::object>(h->obj);
                }
            }
        }

        py::dict kwargs;
        if (norm.size() == 3 && norm.type(2)->code() == TypeCode::Struct) {
            Struct *st                   = norm.get<Struct *>(2);
            const StructType *structType = static_cast<const StructType *>(norm.type(2));
            if (st && structType) {
                for (size_t i = 0; i < structType->size(); ++i) {
                    PythonObjectHolder *h = unwrapPyObject(st->data()[i], ctx);
                    if (!h)
                        return NullSlot;
                    kwargs[py::str(std::string(structType->fieldName(i)))] =
                        py::reinterpret_borrow<py::object>(h->obj);
                }
            }
        }

        py::object result = kwargs.empty() ? callable(*args) : callable(*args, **kwargs);
        return wrapPyObject(result);
    } catch (const py::error_already_set &e) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("python.py_call_kw: \n") + e.what());
    } catch (const std::exception &e) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("python.py_call_kw: \n") + e.what());
    }
}

// py_exec: 执行任意 Python 代码（语句），返回 None
slot_t __python_py_exec__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    if (norm.size() < 1) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_exec: one argument (code string) required");
    }
    if (norm.type(0)->code() != TypeCode::String) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_exec: argument must be string (Python code)");
    }
    String *strObj   = norm.get<String *>(0);
    std::string code = strObj ? strObj->toString() : "";
    try {
        py::exec(code);
        return wrapPyObject(py::none());
    } catch (const py::error_already_set &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_exec: \n") + e.what());
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_exec: \n") + e.what());
    }
}

// py_eval: 计算表达式并返回结果
slot_t __python_py_eval__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    if (norm.size() < 1) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_eval: one argument (expression string) required");
    }
    if (norm.type(0)->code() != TypeCode::String) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_eval: argument must be string (Python expression)");
    }
    String *strObj   = norm.get<String *>(0);
    std::string expr = strObj ? strObj->toString() : "";
    try {
        py::object result = py::eval(expr);
        return wrapPyObject(result);
    } catch (const py::error_already_set &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_eval: \n") + e.what());
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_eval: \n") + e.what());
    }
}

// py_run: 执行指定路径的 Python 脚本文件
slot_t __python_py_run__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    if (norm.size() < 1) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_run: one argument (file path string) required");
    }
    if (norm.type(0)->code() != TypeCode::String) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_run: argument must be string (file path)");
    }
    String *pathObj  = norm.get<String *>(0);
    std::string path = pathObj ? pathObj->toString() : "";
    if (path.empty()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python.py_run: empty file path");
    }
    try {
        py::object scope = py::module_::import("__main__").attr("__dict__");
        py::eval_file(path, scope);
        return wrapPyObject(py::none());
    } catch (const py::error_already_set &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_run: \n") + e.what());
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_run: \n") + e.what());
    }
}

// py_attr: 获取 PyObject 属性，attr 可含 . 表示链式访问（如 "a.b.c"）
slot_t __python_py_attr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    if (norm.size() < 2) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_attr: two arguments (obj, attr_name) required");
    }
    if (norm.type(0)->code() != PyObjectType::typeCode()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_attr: first argument must be PyObject");
    }
    if (norm.type(1)->code() != TypeCode::String) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_attr: second argument must be string (attribute name)");
    }
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    if (!h)
        return NullSlot;
    String *attrObj  = norm.get<String *>(1);
    std::string attr = attrObj ? attrObj->toString() : "";
    if (attr.empty()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python.py_attr: empty attribute name");
    }
    try {
        py::object obj = py::reinterpret_borrow<py::object>(h->obj);
        std::vector<std::string> parts;
        for (size_t i = 0; i < attr.size();) {
            size_t j = attr.find('.', i);
            if (j == std::string::npos) {
                parts.push_back(attr.substr(i));
                break;
            }
            parts.push_back(attr.substr(i, j - i));
            i = j + 1;
        }
        for (const auto &p : parts)
            obj = obj.attr(p.c_str());
        return wrapPyObject(obj);
    } catch (const py::error_already_set &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_attr: \n") + e.what());
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_attr: \n") + e.what());
    }
}

// py_import: 导入 Python 模块，返回 PyObject（模块对象）
slot_t __python_py_import__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    if (norm.size() < 1) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_import: one argument (module name string) required");
    }
    if (norm.type(0)->code() != TypeCode::String) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.py_import: argument must be string (module name)");
    }
    String *strObj   = norm.get<String *>(0);
    std::string name = strObj ? strObj->toString() : "";
    if (name.empty()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python.py_import: empty module name");
    }
    try {
        py::object mod = py::module_::import(name.c_str());
        return wrapPyObject(mod);
    } catch (const py::error_already_set &e) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("python.py_import: \n") + e.what());
    } catch (const std::exception &e) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("python.py_import: \n") + e.what());
    }
}

// py_print / py_println: 打印任意数量 PyObject 以便调试
slot_t __python_py_print__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    py::module_ builtins = py::module_::import("builtins");
    py::object print_fn  = builtins.attr("print");
    try {
        py::tuple args(norm.size());
        for (size_t i = 0; i < norm.size(); ++i) {
            PythonObjectHolder *h = unwrapPyObject(norm.slot(i), ctx);
            if (!h)
                return NullSlot;
            args[i] = h->obj;
        }
        print_fn(*args, py::arg("end") = "", py::arg("flush") = true); // 不换行，立即刷新
        return norm.size() == 0 ? NullSlot : norm.slot(0);
    } catch (const py::error_already_set &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, std::string("python.py_print: \n") + e.what());
    }
}
slot_t __python_py_println__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    py::module_ builtins = py::module_::import("builtins");
    py::object print_fn  = builtins.attr("print");
    try {
        py::tuple args(norm.size());
        for (size_t i = 0; i < norm.size(); ++i) {
            PythonObjectHolder *h = unwrapPyObject(norm.slot(i), ctx);
            if (!h)
                return NullSlot;
            args[i] = h->obj;
        }
        print_fn(*args, py::arg("flush") = true); // 默认换行，立即刷新
        return norm.size() == 0 ? NullSlot : norm.slot(0);
    } catch (const py::error_already_set &e) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("python.py_println: \n") + e.what());
    }
}

slot_t __python_wrap__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    if (norm.size() < 1) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python.wrap: one argument required");
    }
    py::object o = camelToPy(norm, 0, ctx);
    return wrapPyObject(o);
}

// 递归：将 Python 对象按目标类型转为 Camel slot（支持复合类型与嵌套）。
// 成功返回 optional(slot)，失败返回 nullopt。注意不能用 slot==NullSlot 判失败，因整数 0 等的 slot
// 也为 0。
static std::optional<slot_t>
pyToCamel(py::object obj, Type *targetType, camel::core::context::Context &ctx) {
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
                throwRuntimeFault(
                    RuntimeDiag::RuntimeError,
                    "python.unwrap: byte value must be 0-255");
            }
            return toSlot(static_cast<Byte>(static_cast<unsigned>(v)));
        }
        case TypeCode::String: {
            std::string str = py::cast<std::string>(obj);
            return toSlot(String::from(str, mm::autoSpace()));
        }
        case TypeCode::Array: {
            if (!py::isinstance<py::list>(obj)) {
                throwRuntimeFault(
                    RuntimeDiag::RuntimeError,
                    "python.unwrap: array target requires Python list");
            }
            py::list lst               = obj.cast<py::list>();
            const ArrayType *arrayType = static_cast<const ArrayType *>(targetType);
            Type *elemType             = arrayType->elemType();
            size_t n                   = lst.size();
            Array *arr                 = Array::create(mm::autoSpace(), n);
            for (size_t i = 0; i < n; ++i) {
                auto s = pyToCamel(lst[i], elemType, ctx);
                if (!s)
                    return std::nullopt;
                arr->set(i, *s);
            }
            return toSlot(arr);
        }
        case TypeCode::Tuple: {
            if (!py::isinstance<py::tuple>(obj) && !py::isinstance<py::list>(obj)) {
                throwRuntimeFault(
                    RuntimeDiag::RuntimeError,
                    "python.unwrap: tuple target requires Python tuple or list");
            }
            const TupleType *tupleType = static_cast<const TupleType *>(targetType);
            size_t n                   = tupleType->size();
            if (obj.attr("__len__")().cast<py::ssize_t>() != static_cast<py::ssize_t>(n)) {
                throwRuntimeFault(
                    RuntimeDiag::RuntimeError,
                    "python.unwrap: tuple length mismatch");
            }
            Tuple *tuple = Tuple::create(n, mm::autoSpace());
            for (size_t i = 0; i < n; ++i) {
                auto s = pyToCamel(obj[py::int_(i)], tupleType->typeAt(i), ctx);
                if (!s)
                    return std::nullopt;
                tuple->set(i, *s);
            }
            return toSlot(tuple);
        }
        case TypeCode::Struct: {
            if (!py::isinstance<py::dict>(obj)) {
                throwRuntimeFault(
                    RuntimeDiag::RuntimeError,
                    "python.unwrap: struct target requires Python dict");
            }
            py::dict d                   = obj.cast<py::dict>();
            const StructType *structType = static_cast<const StructType *>(targetType);
            size_t n                     = structType->size();
            Struct *st                   = Struct::create(n, mm::autoSpace());
            for (size_t i = 0; i < n; ++i) {
                std::string key = std::string(structType->fieldName(i));
                py::str keyObj(key);
                if (!d.contains(keyObj)) {
                    throwRuntimeFault(
                        RuntimeDiag::RuntimeError,
                        "python.unwrap: struct missing field '" + key + "'");
                }
                auto s = pyToCamel(d[keyObj], structType->typeAt(i), ctx);
                if (!s)
                    return std::nullopt;
                st->set(i, *s);
            }
            return toSlot(st);
        }
        default:
            if (targetType->code() == getPyObjectType()->code())
                return wrapPyObject(obj);
            throwRuntimeFault(
                RuntimeDiag::RuntimeError,
                "python.unwrap: unsupported target type " + targetType->toString());
        }
    } catch (...) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python.unwrap: conversion failed");
    }
}

slot_t __python_upwrap__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    if (!h)
        return NullSlot;
    Type *argType = norm.type(0);
    if (!argType->isOtherType() || argType->code() != getPyObjectType()->code()) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.unwrap: argument must be PyObject<T> with type param");
    }
    auto *o = static_cast<OtherType *>(argType);
    if (o->paramCount() == 0 || !o->params()[0]) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "python.unwrap: PyObject must have one type parameter");
    }
    Type *targetType = o->params()[0];
    auto result      = pyToCamel(h->obj, targetType, ctx);
    return result ? *result : NullSlot;
}
