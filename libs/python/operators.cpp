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
 * Updated: Feb. 22, 2026
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
#include <optional>
#include <pybind11/embed.h>
#include <pybind11/eval.h>
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
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python: unsupported type for wrap");
        return py::none();
    }
}

py::object camelToPy(ArgsView &norm, size_t index, Context &ctx) {
    return camelToPy(norm.type(index), norm.slot(index), ctx);
}

} // namespace

std::unordered_map<std::string, operator_t> getPythonOpsMap() {
    return {
        {"py_call", __python_py_call__},
        {"py_exec", __python_py_exec__},
        {"py_eval", __python_py_eval__},
        {"py_run", __python_py_run__},
        {"py_attr", __python_py_attr__},
        {"py_import", __python_py_import__},
        {"py_print", __python_py_print__},
        {"py_println", __python_py_println__},
        {"wrap", __python_wrap__},
        {"unwrap", __python_upwrap__},
    };
}

// py_call: 调用 Python 函数。首参为字符串时按 "module.attr" 解析，含 . 则第一部分为模块名，否则视为 builtins 下的内置函数
slot_t __python_py_call__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.size() < 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_call: at least one argument (callable or path string) required");
        return NullSlot;
    }
    py::object callable;

    if (norm.type(0)->code() == PyObjectType::typeCode()) {
        PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
        if (!h) return NullSlot;
        callable = py::reinterpret_borrow<py::object>(h->obj);
    } else if (norm.type(0)->code() == TypeCode::String) {
        String *pathObj  = norm.get<String *>(0);
        std::string path = pathObj ? pathObj->toString() : "";
        if (path.empty()) {
            ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python.py_call: empty path");
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
                ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python.py_call: invalid path");
                return NullSlot;
            }
            // 单标识符：先查 __main__（py_exec 定义的函数），再查 builtins
            if (parts.size() == 1) {
                py::object main = py::module_::import("__main__");
                if (py::hasattr(main, parts[0].c_str()))
                    callable = main.attr(parts[0].c_str());
                else
                    callable = py::module_::import("builtins").attr(parts[0].c_str());
            } else {
                py::object obj = py::module_::import(parts[0].c_str());
                for (size_t k = 1; k < parts.size(); ++k)
                    obj = obj.attr(parts[k].c_str());
                callable = obj;
            }
        } catch (const py::error_already_set &e) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit(std::string("python.py_call: \n") + e.what());
            return NullSlot;
        }
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_call: first argument must be PyObject or string (e.g. \"print\", \"builtins.print\", \"mymod.func\")");
        return NullSlot;
    }

    try {
        py::tuple argTuple(norm.size() > 1 ? static_cast<size_t>(norm.size() - 1) : 0);
        for (size_t i = 1; i < norm.size(); ++i)
            argTuple[i - 1] = camelToPy(norm, i, ctx);

        py::object result = callable(*argTuple);
        return wrapPyObject(result);
    } catch (const py::error_already_set &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.pycall: \n") + e.what());
        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.pycall: \n") + e.what());
        return NullSlot;
    }
}

// py_exec: 执行任意 Python 代码（语句），返回 None
slot_t __python_py_exec__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.size() < 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_exec: one argument (code string) required");
        return NullSlot;
    }
    if (norm.type(0)->code() != TypeCode::String) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_exec: argument must be string (Python code)");
        return NullSlot;
    }
    String *strObj = norm.get<String *>(0);
    std::string code = strObj ? strObj->toString() : "";
    try {
        py::exec(code);
        return wrapPyObject(py::none());
    } catch (const py::error_already_set &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_exec: \n") + e.what());
        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_exec: \n") + e.what());
        return NullSlot;
    }
}

// py_eval: 计算表达式并返回结果
slot_t __python_py_eval__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.size() < 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_eval: one argument (expression string) required");
        return NullSlot;
    }
    if (norm.type(0)->code() != TypeCode::String) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_eval: argument must be string (Python expression)");
        return NullSlot;
    }
    String *strObj = norm.get<String *>(0);
    std::string expr = strObj ? strObj->toString() : "";
    try {
        py::object result = py::eval(expr);
        return wrapPyObject(result);
    } catch (const py::error_already_set &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_eval: \n") + e.what());
        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_eval: \n") + e.what());
        return NullSlot;
    }
}

// py_run: 执行指定路径的 Python 脚本文件
slot_t __python_py_run__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.size() < 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_run: one argument (file path string) required");
        return NullSlot;
    }
    if (norm.type(0)->code() != TypeCode::String) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_run: argument must be string (file path)");
        return NullSlot;
    }
    String *pathObj = norm.get<String *>(0);
    std::string path = pathObj ? pathObj->toString() : "";
    if (path.empty()) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python.py_run: empty file path");
        return NullSlot;
    }
    try {
        py::object scope = py::module_::import("__main__").attr("__dict__");
        py::eval_file(path, scope);
        return wrapPyObject(py::none());
    } catch (const py::error_already_set &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_run: \n") + e.what());
        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_run: \n") + e.what());
        return NullSlot;
    }
}

// py_attr: 获取 PyObject 属性，attr 可含 . 表示链式访问（如 "a.b.c"）
slot_t __python_py_attr__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.size() < 2) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_attr: two arguments (obj, attr_name) required");
        return NullSlot;
    }
    if (norm.type(0)->code() != PyObjectType::typeCode()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_attr: first argument must be PyObject");
        return NullSlot;
    }
    if (norm.type(1)->code() != TypeCode::String) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_attr: second argument must be string (attribute name)");
        return NullSlot;
    }
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    if (!h) return NullSlot;
    String *attrObj = norm.get<String *>(1);
    std::string attr = attrObj ? attrObj->toString() : "";
    if (attr.empty()) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python.py_attr: empty attribute name");
        return NullSlot;
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
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_attr: \n") + e.what());
        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_attr: \n") + e.what());
        return NullSlot;
    }
}

// py_import: 导入 Python 模块，返回 PyObject（模块对象）
slot_t __python_py_import__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.size() < 1) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_import: one argument (module name string) required");
        return NullSlot;
    }
    if (norm.type(0)->code() != TypeCode::String) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.py_import: argument must be string (module name)");
        return NullSlot;
    }
    String *strObj = norm.get<String *>(0);
    std::string name = strObj ? strObj->toString() : "";
    if (name.empty()) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python.py_import: empty module name");
        return NullSlot;
    }
    try {
        py::object mod = py::module_::import(name.c_str());
        return wrapPyObject(mod);
    } catch (const py::error_already_set &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_import: \n") + e.what());
        return NullSlot;
    } catch (const std::exception &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_import: \n") + e.what());
        return NullSlot;
    }
}

// py_print / py_println: 打印任意数量 PyObject 以便调试
slot_t __python_py_print__(ArgsView &with, ArgsView &norm, Context &ctx) {
    py::module_ builtins = py::module_::import("builtins");
    py::object print_fn  = builtins.attr("print");
    try {
        py::tuple args(norm.size());
        for (size_t i = 0; i < norm.size(); ++i) {
            PythonObjectHolder *h = unwrapPyObject(norm.slot(i), ctx);
            if (!h) return NullSlot;
            args[i] = h->obj;
        }
        print_fn(*args, py::arg("end") = "", py::arg("flush") = true); // 不换行，立即刷新
        return norm.size() == 0 ? NullSlot : norm.slot(0);
    } catch (const py::error_already_set &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_print: \n") + e.what());
        return NullSlot;
    }
}
slot_t __python_py_println__(ArgsView &with, ArgsView &norm, Context &ctx) {
    py::module_ builtins = py::module_::import("builtins");
    py::object print_fn  = builtins.attr("print");
    try {
        py::tuple args(norm.size());
        for (size_t i = 0; i < norm.size(); ++i) {
            PythonObjectHolder *h = unwrapPyObject(norm.slot(i), ctx);
            if (!h) return NullSlot;
            args[i] = h->obj;
        }
        print_fn(*args, py::arg("flush") = true); // 默认换行，立即刷新
        return norm.size() == 0 ? NullSlot : norm.slot(0);
    } catch (const py::error_already_set &e) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(std::string("python.py_println: \n") + e.what());
        return NullSlot;
    }
}

slot_t __python_wrap__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.size() < 1) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("python.wrap: one argument required");
        return NullSlot;
    }
    py::object o = camelToPy(norm, 0, ctx);
    return wrapPyObject(o);
}

// 递归：将 Python 对象按目标类型转为 Camel slot（支持复合类型与嵌套）。
// 成功返回 optional(slot)，失败返回 nullopt。注意不能用 slot==NullSlot 判失败，因整数 0 等的 slot 也为 0。
static std::optional<slot_t> pyToCamel(py::object obj, Type *targetType, Context &ctx) {
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
                    .commit("python.unwrap: byte value must be 0-255");
                return std::nullopt;
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
                    .commit("python.unwrap: array target requires Python list");
                return std::nullopt;
            }
            py::list lst              = obj.cast<py::list>();
            const ArrayType *arrayType = static_cast<const ArrayType *>(targetType);
            Type *elemType            = arrayType->elemType();
            size_t n                  = lst.size();
            Array *arr                = Array::create(mm::autoSpace(), n);
            for (size_t i = 0; i < n; ++i) {
                auto s = pyToCamel(lst[i], elemType, ctx);
                if (!s) return std::nullopt;
                arr->set(i, *s);
            }
            return toSlot(arr);
        }
        case TypeCode::Tuple: {
            if (!py::isinstance<py::tuple>(obj) && !py::isinstance<py::list>(obj)) {
                ctx.rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit("python.unwrap: tuple target requires Python tuple or list");
                return std::nullopt;
            }
            const TupleType *tupleType = static_cast<const TupleType *>(targetType);
            size_t n                   = tupleType->size();
            if (obj.attr("__len__")().cast<py::ssize_t>() != static_cast<py::ssize_t>(n)) {
                ctx.rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit("python.unwrap: tuple length mismatch");
                return std::nullopt;
            }
            Tuple *tuple = Tuple::create(n, mm::autoSpace());
            for (size_t i = 0; i < n; ++i) {
                auto s = pyToCamel(obj[py::int_(i)], tupleType->typeAt(i), ctx);
                if (!s) return std::nullopt;
                tuple->set(i, *s);
            }
            return toSlot(tuple);
        }
        case TypeCode::Struct: {
            if (!py::isinstance<py::dict>(obj)) {
                ctx.rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit("python.unwrap: struct target requires Python dict");
                return std::nullopt;
            }
            py::dict d                 = obj.cast<py::dict>();
            const StructType *structType = static_cast<const StructType *>(targetType);
            size_t n                   = structType->size();
            Struct *st                 = Struct::create(n, mm::autoSpace());
            for (size_t i = 0; i < n; ++i) {
                std::string key  = std::string(structType->fieldName(i));
                py::str keyObj(key);
                if (!d.contains(keyObj)) {
                    ctx.rtmDiags()
                        ->of(RuntimeDiag::RuntimeError)
                        .commit("python.unwrap: struct missing field '" + key + "'");
                    return std::nullopt;
                }
                auto s = pyToCamel(d[keyObj], structType->typeAt(i), ctx);
                if (!s) return std::nullopt;
                st->set(i, *s);
            }
            return toSlot(st);
        }
        default:
            if (targetType->code() == getPyObjectType()->code())
                return wrapPyObject(obj);
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("python.unwrap: unsupported target type " + targetType->toString());
            return std::nullopt;
        }
    } catch (...) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.unwrap: conversion failed");
        return std::nullopt;
    }
}

slot_t __python_upwrap__(ArgsView &with, ArgsView &norm, Context &ctx) {
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    if (!h)
        return NullSlot;
    Type *argType = norm.type(0);
    if (!argType->isOtherType() || argType->code() != getPyObjectType()->code()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.unwrap: argument must be PyObject<T> with type param");
        return NullSlot;
    }
    auto *o = static_cast<OtherType *>(argType);
    if (o->paramCount() == 0 || !o->params()[0]) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("python.unwrap: PyObject must have one type parameter");
        return NullSlot;
    }
    Type *targetType = o->params()[0];
    auto result      = pyToCamel(h->obj, targetType, ctx);
    return result ? *result : NullSlot;
}
