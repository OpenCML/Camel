// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/mm/block.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/bytes.h"
#include "camel/core/rtdata/collection.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/base.h"
#include "camel/core/type/bytes.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/other.h"
#include "ensure.h"

#include <cmath>
#include <limits>
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

constexpr std::string_view kPythonObjectKind = "python.PyObject";

void deletePythonObjectHolder(void *resource) {
    auto *holder = static_cast<PythonObjectHolder *>(resource);
    if (!holder) {
        return;
    }
    if (Py_IsInitialized()) {
        py::gil_scoped_acquire gil;
        delete holder;
        return;
    }
    delete holder;
}

ForeignResourceDescriptor pythonObjectDescriptor() {
    return ForeignResourceDescriptor{
        .kind                    = std::string(kPythonObjectKind),
        .deleter                 = deletePythonObjectHolder,
        .clone                   = {},
        .trace                   = {},
        .onMoved                 = {},
        .containsCamelReferences = false,
        .finalizable             = true,
        .movable                 = true,
        .pinned                  = false,
    };
}

slot_t wrapPyObject(py::object o) {
    auto *holder = new PythonObjectHolder{std::move(o)};
    try {
        return toSlot(
            ForeignResourceObject::create(mm::autoSpace(), pythonObjectDescriptor(), holder));
    } catch (...) {
        deletePythonObjectHolder(holder);
        throw;
    }
}

ForeignResourceObject *
unwrapPyObjectWrapper(slot_t s, camel::core::context::Context &ctx, bool allowDisposed = false) {
    if (s == NullSlot) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python: expected non-null PyObject");
    }
    auto *object  = fromSlot<Object *>(s);
    auto *wrapper = dynamic_cast<ForeignResourceObject *>(object);
    if (!wrapper || wrapper->kind() != kPythonObjectKind) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python: expected PyObject foreign resource");
    }
    if (!allowDisposed && wrapper->disposed()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python: PyObject has been disposed");
    }
    return wrapper;
}

PythonObjectHolder *unwrapPyObject(slot_t s, camel::core::context::Context &ctx) {
    return unwrapPyObjectWrapper(s, ctx)->resourceAs<PythonObjectHolder>();
}

/// Releases a block that borrows a Python bytes object's buffer: dropping the holder drops the
/// reference (and restores the GIL, like every other Python resource release).
void releasePythonBytes(void *context) noexcept {
    deletePythonObjectHolder(context);
}

// 递归：按类型与 slot 将 Camel 值转为 Python 对象（支持复合类型与嵌套）
py::object camelToPy(camel::core::type::Type *t, slot_t s, camel::core::context::Context &ctx) {
    if (isBytesType(t)) {
        // Python bytes is immutable, so this is the one copy; the value stays a block in Camel.
        auto *bytes = fromSlot<BytesObject *>(s);
        return py::bytes(bytes->view().data(), bytes->size());
    }
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
            lst.append(camelToPy(elemType, arr->getSlot(i), ctx));
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

// The text of a Python exception: its class and what it says, then the traceback. The class name
// is what a program tells `KeyError` from `ImportError` by, so it comes first.
std::string pythonErrorMessage(const py::error_already_set &e) {
    std::string summary = "Python exception";
    try {
        const std::string cls = py::str(e.type().attr("__name__")).cast<std::string>();
        const std::string text = py::str(e.value()).cast<std::string>();
        summary = text.empty() ? cls : cls + ": " + text;
    } catch (...) {
    }
    std::string trace = e.what();
    // pybind11 ends its rendering with the same "Class: text" line.
    if (trace.size() >= summary.size() &&
        trace.compare(trace.size() - summary.size(), summary.size(), summary) == 0) {
        trace.resize(trace.size() - summary.size());
    }
    while (!trace.empty() && (trace.back() == '\n' || trace.back() == ' ')) {
        trace.pop_back();
    }
    return trace.empty() ? summary : summary + "\n" + trace;
}

// A Python exception is the failure of an external system: the call's result is an error value of
// kind `PythonError` (stdlib conventions 5.1), and the program decides what to do with it.
slot_t pythonFailure(std::string_view who, const py::error_already_set &e) {
    return makeError("PythonError", pythonErrorMessage(e), "<python::" + std::string(who) + ">");
}

// A successful result of an operator whose result type is `PyObject!`.
slot_t pythonOk(ArgsView &norm, py::object value) {
    const auto *resultType = static_cast<const TupleType *>(norm.resultType());
    return makeOk(wrapPyObject(std::move(value)), resultType);
}

} // namespace

std::unordered_map<std::string, operator_t> getPythonOpsMap() {
    return {
        {"exec", __python_exec__},
        {"eval", __python_eval__},
        {"run_file", __python_run_file__},
        {"import_module", __python_import_module__},
        {"attr", __python_attr__},
        {"call", __python_call__},
        {"to_string", __python_to_string__},
        {"py_wrap", __python_wrap__},
        {"py_unwrap", __python_upwrap__},
    };
}

// The PyObject a slot holds, read as a borrowed Python object.
static py::object pythonObjectOf(slot_t slot, camel::core::context::Context &ctx) {
    PythonObjectHolder *holder = unwrapPyObject(slot, ctx);
    return holder ? py::reinterpret_borrow<py::object>(holder->obj) : py::none();
}

// `args->call<fn, kwargs>`: calls the Python callable `fn` with the PyObjects the array holds as
// positional arguments, in order, and the entries of `kwargs` (a `Map<string, PyObject>?`, null
// for none) as keyword arguments.
slot_t __python_call__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    // The interpreter is created at the operator's first use, not by `import python`.
    ensurePythonInterpreter(ctx);
    py::gil_scoped_acquire gil;
    try {
        py::object callable = pythonObjectOf(with.slot(0), ctx);
        if (callable.is_none()) {
            return makeError("PythonError", "the callable is None", "<python::call>");
        }
        Array *arr = norm.get<Array *>(0);
        py::tuple args(arr ? arr->size() : 0);
        for (size_t i = 0; arr && i < arr->size(); ++i) {
            args[i] = pythonObjectOf(arr->getSlot(i), ctx);
        }
        py::dict kwargs;
        if (with.size() >= 2) {
            slot_t slot = with.slot(1);
            bool given  = true;
            auto *nullable = asNullable(with.type(1));
            Type *mapType  = with.type(1);
            if (nullable != nullptr) {
                given   = !isNullValue(slot);
                slot    = nullablePayload(slot);
                mapType = nullable->payloadType();
            } else if (!dynamic_cast<camel::core::type::MapType *>(mapType)) {
                given = false; // the type of `null`
            }
            if (given && mapType != nullptr) {
                const auto *type = static_cast<const camel::core::type::MapType *>(mapType);
                camel::core::rtdata::table::forEachEntry(
                    fromSlot<camel::core::rtdata::Table *>(slot),
                    type,
                    [&](slot_t key, slot_t value) {
                        kwargs[py::str(fromSlot<String *>(key)->toString())] =
                            pythonObjectOf(value, ctx);
                    });
            }
        }
        return pythonOk(norm, kwargs.empty() ? callable(*args) : callable(*args, **kwargs));
    } catch (const py::error_already_set &e) {
        return pythonFailure("call", e);
    }
}

// The text argument `index` of an operator, or a fault when the call did not pass one.
static std::string textArgument(ArgsView &norm, const char *who, const char *what) {
    if (norm.size() < 1) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("python.") + who + ": one argument (" + what + ") required");
    }
    if (norm.type(0)->code() != TypeCode::String) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            std::string("python.") + who + ": argument must be string (" + what + ")");
    }
    String *strObj = norm.get<String *>(0);
    return strObj ? strObj->toString() : "";
}

// exec: 执行任意 Python 代码（语句）
slot_t __python_exec__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    // The interpreter is created at the operator's first use, not by `import python`.
    ensurePythonInterpreter(ctx);
    py::gil_scoped_acquire gil;
    const std::string code = textArgument(norm, "exec", "code string");
    try {
        py::exec(code);
        return makeOk(NullSlot, static_cast<const TupleType *>(norm.resultType()));
    } catch (const py::error_already_set &e) {
        return pythonFailure("exec", e);
    }
}

// eval: 计算表达式并返回结果
slot_t __python_eval__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    // The interpreter is created at the operator's first use, not by `import python`.
    ensurePythonInterpreter(ctx);
    py::gil_scoped_acquire gil;
    const std::string expr = textArgument(norm, "eval", "expression string");
    try {
        return pythonOk(norm, py::eval(expr));
    } catch (const py::error_already_set &e) {
        return pythonFailure("eval", e);
    }
}

// run_file: 执行指定路径的 Python 脚本文件
slot_t __python_run_file__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    // The interpreter is created at the operator's first use, not by `import python`.
    ensurePythonInterpreter(ctx);
    py::gil_scoped_acquire gil;
    const std::string path = textArgument(norm, "run_file", "file path string");
    if (path.empty()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python.run_file: empty file path");
    }
    try {
        py::object scope = py::module_::import("__main__").attr("__dict__");
        py::eval_file(path, scope);
        return makeOk(NullSlot, static_cast<const TupleType *>(norm.resultType()));
    } catch (const py::error_already_set &e) {
        return pythonFailure("run_file", e);
    }
}

// obj->attr<name>: 获取 PyObject 属性，name 可含 . 表示链式访问（如 "a.b.c"）
slot_t __python_attr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    // The interpreter is created at the operator's first use, not by `import python`.
    ensurePythonInterpreter(ctx);
    py::gil_scoped_acquire gil;
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    String *attrObj       = with.get<String *>(0);
    std::string attr      = attrObj ? attrObj->toString() : "";
    if (attr.empty()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python.attr: empty attribute name");
    }
    try {
        py::object obj = py::reinterpret_borrow<py::object>(h->obj);
        for (size_t i = 0; i < attr.size();) {
            size_t j = attr.find('.', i);
            if (j == std::string::npos) {
                obj = obj.attr(attr.substr(i).c_str());
                break;
            }
            obj = obj.attr(attr.substr(i, j - i).c_str());
            i   = j + 1;
        }
        return pythonOk(norm, obj);
    } catch (const py::error_already_set &e) {
        return pythonFailure("attr", e);
    }
}

// import_module: 导入 Python 模块，返回 PyObject（模块对象）
slot_t __python_import_module__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    // The interpreter is created at the operator's first use, not by `import python`.
    ensurePythonInterpreter(ctx);
    py::gil_scoped_acquire gil;
    const std::string name = textArgument(norm, "import_module", "module name string");
    if (name.empty()) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python.import_module: empty module name");
    }
    try {
        return pythonOk(norm, py::module_::import(name.c_str()));
    } catch (const py::error_already_set &e) {
        return pythonFailure("import_module", e);
    }
}

// to_string: the `str()` of a Python object, as text.
slot_t __python_to_string__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    // The interpreter is created at the operator's first use, not by `import python`.
    ensurePythonInterpreter(ctx);
    py::gil_scoped_acquire gil;
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    try {
        const std::string text = py::str(h->obj).cast<std::string>();
        return makeOk(
            toSlot(String::from(text, mm::autoSpace())),
            static_cast<const TupleType *>(norm.resultType()));
    } catch (const py::error_already_set &e) {
        return pythonFailure("to_string", e);
    }
}

slot_t __python_wrap__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    // The interpreter is created at the operator's first use, not by `import python`.
    ensurePythonInterpreter(ctx);
    py::gil_scoped_acquire gil;
    if (norm.size() < 1) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "python.wrap: one argument required");
    }
    py::object o = camelToPy(norm, 0, ctx);
    return wrapPyObject(o);
}

// The Python type of an object, for a message.
static std::string pythonTypeName(const py::handle &obj) {
    try {
        return py::str(py::type::of(obj).attr("__name__")).cast<std::string>();
    } catch (...) {
        return "an object";
    }
}

// 递归：将 Python 对象按目标类型转为 Camel slot（支持复合类型与嵌套）。
//
// What converts is a table, not whatever pybind11 would accept: a Camel value is not made from a
// Python object of another kind (design principles 5.1). `bool` takes a bool only (not `5`), the
// integers take an int that fits and not a bool or a float, the floats take a float or an int and
// refuse a number the type cannot hold (`1e300` for an f32), `string` takes a str (not bytes), and
// `Bytes` takes bytes. On failure `problem` says where (a path from the root) and what was found.
static bool pyToCamel(
    py::object obj, Type *targetType, camel::core::context::Context &ctx, const std::string &path,
    slot_t &out, std::string &problem) {
    const auto mismatch = [&](const std::string &expected) {
        problem = path + ": expected " + expected + ", found " + pythonTypeName(obj);
        return false;
    };
    try {
        if (isBytesType(targetType)) {
            if (!py::isinstance<py::bytes>(obj)) {
                return mismatch("bytes");
            }
            char *data   = nullptr;
            Py_ssize_t n = 0;
            if (PyBytes_AsStringAndSize(obj.ptr(), &data, &n) != 0) {
                throw py::error_already_set();
            }
            auto *holder     = new PythonObjectHolder{obj};
            mm::Block *block = nullptr;
            try {
                block = mm::Block::adoptExternal(
                    data, static_cast<size_t>(n), mm::BlockRelease{&releasePythonBytes, holder});
            } catch (...) {
                deletePythonObjectHolder(holder);
                throw;
            }
            // The value adopts the block without copying; the holder keeps the Python bytes
            // object alive until the block's last reference is gone.
            out = toSlot(BytesObject::adopt(block, static_cast<size_t>(n), mm::autoSpace()));
            return true;
        }
        switch (targetType->code()) {
        case TypeCode::Int32:
        case TypeCode::Int64: {
            if (PyBool_Check(obj.ptr()) || !PyLong_Check(obj.ptr())) {
                return mismatch("an int");
            }
            int overflow          = 0;
            const long long value = PyLong_AsLongLongAndOverflow(obj.ptr(), &overflow);
            if (overflow != 0 || (value == -1 && PyErr_Occurred())) {
                PyErr_Clear();
                problem = path + ": the int does not fit a 64-bit integer";
                return false;
            }
            if (targetType->code() == TypeCode::Int32) {
                if (value < INT32_MIN || value > INT32_MAX) {
                    problem = path + ": the int " + std::to_string(value) + " does not fit an i32";
                    return false;
                }
                out = toSlot(static_cast<Int32>(value));
            } else {
                out = toSlot(static_cast<Int64>(value));
            }
            return true;
        }
        case TypeCode::Float32:
        case TypeCode::Float64: {
            if (PyBool_Check(obj.ptr()) || (!PyFloat_Check(obj.ptr()) && !PyLong_Check(obj.ptr()))) {
                return mismatch("a float");
            }
            const double value = PyFloat_AsDouble(obj.ptr());
            if (value == -1.0 && PyErr_Occurred()) {
                PyErr_Clear();
                problem = path + ": the int is too large for a float";
                return false;
            }
            if (targetType->code() == TypeCode::Float32) {
                if (std::isfinite(value) && std::fabs(value) > std::numeric_limits<float>::max()) {
                    problem = path + ": the number " + py::str(obj).cast<std::string>() +
                              " does not fit an f32";
                    return false;
                }
                out = toSlot(static_cast<Float32>(value));
            } else {
                out = toSlot(static_cast<Float64>(value));
            }
            return true;
        }
        case TypeCode::Bool:
            if (!PyBool_Check(obj.ptr())) {
                return mismatch("a bool");
            }
            out = toSlot(obj.ptr() == Py_True);
            return true;
        case TypeCode::String: {
            if (!PyUnicode_Check(obj.ptr())) {
                return mismatch("a str");
            }
            Py_ssize_t size       = 0;
            const char *utf8      = PyUnicode_AsUTF8AndSize(obj.ptr(), &size);
            if (utf8 == nullptr) {
                PyErr_Clear();
                problem = path + ": the str is not valid UTF-8";
                return false;
            }
            out = toSlot(String::from(std::string(utf8, static_cast<size_t>(size)), mm::autoSpace()));
            return true;
        }
        case TypeCode::Array: {
            if (!py::isinstance<py::list>(obj)) {
                return mismatch("a list");
            }
            py::list lst               = obj.cast<py::list>();
            const ArrayType *arrayType = static_cast<const ArrayType *>(targetType);
            Type *elemType             = arrayType->elemType();
            size_t n                   = lst.size();
            Array *arr                 = Array::create(mm::autoSpace(), n);
            for (size_t i = 0; i < n; ++i) {
                slot_t element = NullSlot;
                if (!pyToCamel(
                        lst[i], elemType, ctx, path + "[" + std::to_string(i) + "]", element,
                        problem))
                    return false;
                arr->set(i, element, arrayType);
            }
            out = toSlot(arr);
            return true;
        }
        case TypeCode::Tuple: {
            if (!py::isinstance<py::tuple>(obj) && !py::isinstance<py::list>(obj)) {
                return mismatch("a tuple");
            }
            const TupleType *tupleType = static_cast<const TupleType *>(targetType);
            size_t n                   = tupleType->size();
            const auto length          = static_cast<size_t>(py::len(obj));
            if (length != n) {
                problem = path + ": expected a tuple of " + std::to_string(n) + " element(s), found " +
                          std::to_string(length);
                return false;
            }
            Tuple *tuple = Tuple::create(n, mm::autoSpace());
            for (size_t i = 0; i < n; ++i) {
                slot_t element = NullSlot;
                if (!pyToCamel(
                        obj[py::int_(i)], tupleType->typeAt(i), ctx,
                        path + "[" + std::to_string(i) + "]", element, problem))
                    return false;
                tuple->set(i, element, tupleType);
            }
            out = toSlot(tuple);
            return true;
        }
        case TypeCode::Struct: {
            if (!py::isinstance<py::dict>(obj)) {
                return mismatch("a dict");
            }
            py::dict d                   = obj.cast<py::dict>();
            const StructType *structType = static_cast<const StructType *>(targetType);
            size_t n                     = structType->size();
            Struct *st                   = Struct::create(n, mm::autoSpace());
            for (size_t i = 0; i < n; ++i) {
                std::string key = std::string(structType->fieldName(i));
                py::str keyObj(key);
                if (!d.contains(keyObj)) {
                    problem = path + ": missing field '" + key + "'";
                    return false;
                }
                slot_t field = NullSlot;
                if (!pyToCamel(d[keyObj], structType->typeAt(i), ctx, path + "." + key, field, problem))
                    return false;
                st->set(i, field, structType);
            }
            out = toSlot(st);
            return true;
        }
        default:
            if (targetType->code() == getPyObjectType()->code()) {
                out = wrapPyObject(obj);
                return true;
            }
            problem = path + ": a Python value cannot become " + targetType->toString();
            return false;
        }
    } catch (const py::error_already_set &e) {
        problem = path + ": " + pythonErrorMessage(e);
        return false;
    }
}

slot_t __python_upwrap__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    // The interpreter is created at the operator's first use, not by `import python`.
    ensurePythonInterpreter(ctx);
    py::gil_scoped_acquire gil;
    PythonObjectHolder *h = unwrapPyObject(norm.slot(0), ctx);
    Type *argType         = norm.type(0);
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
    slot_t converted = NullSlot;
    std::string problem;
    if (!pyToCamel(h->obj, targetType, ctx, "value", converted, problem)) {
        return makeError("PythonTypeError", problem, "<python::py_unwrap>");
    }
    return makeOk(converted, static_cast<const TupleType *>(norm.resultType()));
}
