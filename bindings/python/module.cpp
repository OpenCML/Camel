// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The CApi extension behind `import camel`: a Python project calls the functions of a
 * Camel module and the arrays cross in both directions through DLPack, without a copy.
 *
 * What it is. A host of libcamel, like `camel` itself: one `Context`, module search paths from
 * CAMEL_HOME / CAMEL_PACKAGES / the directories the caller names, and one call is
 *
 *   Definitions::libraryFunction(module, function)   -> the function's graph, built once
 *   NodeVMSchedPass::callGraph(program, graph, args) -> the value, on the node VM
 *
 * `libraryFunction` is the runtime's own "a native caller runs a function written in Camel" door
 * (include/camel/runtime/definitions.h): the same one a pass written in Camel is registered
 * through, so this binding adds no second way to run a graph. The scheduler is the node VM, the
 * one every `camel <file> std::nvm` run uses.
 *
 * Arrays. A Python argument that has `__dlpack__` (NumPy, PyTorch, CuPy, JAX) is handed over as
 * a DLPack capsule, and `core::array::fromDLPack` adopts the producer's memory as an external
 * data block: no copy, and the producer's deleter runs when the last Camel array that read it is
 * gone. A returned array goes back through `core::array::toDLPack`, which retains the block and
 * hands out its address. Both directions are the contract of docs/design/arrays-memory 3.2.
 *
 * Faults. A module that does not compile raises a DiagnosticError carrying the first error of
 * its own diagnostics container: name, catalogue code, message, suggestion, file, line, column,
 * and the report rendered as `camel` renders it. A fault during the call raises a FaultError
 * built from the error value's Error structure (kind, message, location -- core/rtdata/tagged.h)
 * or from the RuntimeFault the runtime threw. Neither is reworded here: the texts are the ones
 * the compiler and the runtime produced.
 *
 * What it does not do. Only a non-generic exported function is callable (`libraryFunction`
 * refuses a generic one, which has no single graph); passing a type argument for one is a
 * question for the language author, not a choice for this file.
 * Only the numeric element types DLPack names cross, and only host memory, contiguous, one lane
 * (the limits of fromDLPack/toDLPack in src/core/array/dlpack.cpp).
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#include "camel/core/array/dlpack.h"
#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/mm/block.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/module/userdef.h"
#include "camel/core/rtdata.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/definitions.h"
#include "camel/runtime/graph.h"
#include "camel/utils/dll_path.h"
#include "camel/utils/install_layout.h"
#include "camel/utils/path.h"
#include "passes/sched/nodevm/nodevm.h"

#include <array>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using camel::core::context::context_ptr_t;
using camel::core::mm::RootHandle;
using camel::core::type::ArrayType;
using camel::core::type::FunctionType;
using camel::core::type::TupleType;
using camel::core::type::Type;
using camel::core::type::TypeCode;

// `Array`, `String`, `Struct` and `Tuple` are the runtime's value classes at global scope
// (include/camel/core/rtdata/*.h); the nested names are the templates that build them.

// ---- the exception types ---------------------------------------------------

PyObject *CamelErrorType      = nullptr;
PyObject *DiagnosticErrorType = nullptr;
PyObject *FaultErrorType      = nullptr;

/// One diagnostic or fault, in the parts a Python caller wants to branch on.
struct Report {
    bool fault = false;      ///< a fault of the run, not a diagnostic of the compiler
    std::string name;        ///< SemanticDiag::UnknownName, or the error value's kind
    std::string code;        ///< "0x03001234" when the diagnostic has a catalogue code
    std::string message;
    std::string suggestion;
    std::string file;        ///< the module's path, or the name the error value recorded
    size_t line   = 0;       ///< 1-based; 0 when the report has no position
    size_t column = 0;       ///< 1-based
    std::string stage;       ///< "parse", "typecheck", "pass:<name>", "run"
    std::string text;        ///< the report as `camel` renders it
};

/// What `str(exception)` gives: the position first, then the name, the code and the sentence.
/// The parts are also attributes, so a caller never has to parse this line back.
std::string oneLineOf(const Report &r) {
    std::string out;
    if (!r.file.empty()) {
        out += r.file;
        out += ":" + std::to_string(r.line) + ":" + std::to_string(r.column) + ": ";
    }
    out += "error[" + r.name + "]";
    if (!r.code.empty()) {
        out += " (" + r.code + ")";
    }
    out += ": " + r.message;
    if (!r.suggestion.empty()) {
        out += "\nhelp: " + r.suggestion;
    }
    return out;
}

void setTextAttribute(PyObject *exception, const char *name, const std::string &value) {
    PyObject *text = PyUnicode_FromStringAndSize(value.c_str(), Py_ssize_t(value.size()));
    if (text == nullptr) {
        PyErr_Clear();
        return;
    }
    if (PyObject_SetAttrString(exception, name, text) != 0) {
        PyErr_Clear();
    }
    Py_DECREF(text);
}

void setIntAttribute(PyObject *exception, const char *name, size_t value) {
    PyObject *number = PyLong_FromSize_t(value);
    if (number == nullptr) {
        PyErr_Clear();
        return;
    }
    if (PyObject_SetAttrString(exception, name, number) != 0) {
        PyErr_Clear();
    }
    Py_DECREF(number);
}

/// Raises the exception `r` describes. Every caller returns as soon as it has called this: the
/// exceptions here are set through `PyErr_SetObject`, so a C++ frame that went on to build a
/// value would hand Python a result and an error at once.
void raiseReport(const Report &r) {
    const std::string line = oneLineOf(r);
    PyObject *type         = r.fault ? FaultErrorType : DiagnosticErrorType;
    PyObject *exception    = PyObject_CallFunction(type, "s", line.c_str());
    if (exception == nullptr) {
        // The exception class itself refused to be built; the error that call set is the best
        // report left, and clearing it would hide a defect of this module.
        PyErr_Print();
        PyErr_SetString(PyExc_RuntimeError, line.c_str());
        return;
    }
    setTextAttribute(exception, "name", r.name);
    setTextAttribute(exception, "code", r.code);
    setTextAttribute(exception, "message", r.message);
    setTextAttribute(exception, "suggestion", r.suggestion);
    setTextAttribute(exception, "file", r.file);
    setTextAttribute(exception, "stage", r.stage);
    setTextAttribute(exception, "camel_text", r.text);
    setIntAttribute(exception, "line", r.line);
    setIntAttribute(exception, "column", r.column);
    PyErr_SetObject(type, exception);
    Py_DECREF(exception);
}

/// Report and leave: what a failure does inside a function that returns a Python object.
#define CAMEL_RAISE(report)  \
    do {                     \
        raiseReport(report); \
        return nullptr;      \
    } while (false)

// ---- the host --------------------------------------------------------------

/// One Camel `Context` for the process, made on the first session. A second session reuses it:
/// the runtime keeps every compiled library in it, so a context per session would compile the
/// same module again for every Python `load`.
static context_ptr_t ensureHost(const std::string &entryDir, const std::vector<std::string> &dirs) {
    static context_ptr_t shared = [&] {
        camel::utils::setupLibrarySearchPathForApp();
        std::string dir = entryDir;
        if (dir.empty()) {
            dir = camel::utils::pathToUtf8(std::filesystem::current_path());
        }
        camel::utils::ModuleSearchPathOptions options;
        options.includeDirs = dirs;

        camel::core::context::EntryConfig config;
        config.entryDir    = dir;
        config.entryFile   = "";
        config.searchPaths = camel::utils::buildModuleSearchPaths(dir, options);
        return camel::core::context::Context::create(config, camel::core::error::DiagsConfig{});
    }();
    return shared;
}

// ---- argument conversion ---------------------------------------------------

/// The slots of one call, with the heap objects rooted: callGraph may reach a safepoint, and an
/// argument must stay where it is across it (docs/technical/36_gc_handle_rules.md).
struct Arguments {
    std::vector<slot_t> slots;
    std::deque<RootHandle> roots;

    void root(const camel::core::type::Type *type, camel::core::rtdata::Object *object) {
        roots.emplace_back(camel::core::mm::autoSpace(), object, type, "python argument");
    }
};

std::optional<std::string> textOf(PyObject *object, std::string &out) {
    if (!PyUnicode_Check(object)) {
        return "expected a str";
    }
    Py_ssize_t size = 0;
    const char *utf8 = PyUnicode_AsUTF8AndSize(object, &size);
    if (utf8 == nullptr) {
        PyErr_Clear();
        return "the str is not valid UTF-8";
    }
    out.assign(utf8, size_t(size));
    return std::nullopt;
}

/// The Camel array a Python object stands for, through DLPack. An object with `__dlpack__`
/// (NumPy, PyTorch, CuPy, JAX) is asked for its capsule; a capsule is taken as it is, which is
/// what the protocol says a consumer may be handed and what lets a project without any of those
/// libraries still pass an array. Either way the capsule is consumed: its name becomes
/// "used_dltensor" and the memory is adopted by this array, whose block releases it through the
/// producer's deleter.
std::optional<std::string> adoptArray(PyObject *object, Array *&out, const std::string &what) {
    PyObject *capsule = nullptr;
    if (PyCapsule_CheckExact(object)) {
        capsule = object;
        Py_INCREF(capsule);
    } else {
        capsule = PyObject_CallMethod(object, "__dlpack__", nullptr);
        if (capsule == nullptr) {
            PyErr_Clear();
            return "a " + what + " array must offer __dlpack__ (NumPy, PyTorch, CuPy and JAX "
                   "do), or be a DLPack capsule";
        }
    }
    if (!PyCapsule_CheckExact(capsule)) {
        Py_DECREF(capsule);
        return "its __dlpack__ did not return a capsule";
    }
    void *raw = PyCapsule_GetPointer(capsule, "dltensor");
    if (raw == nullptr) {
        PyErr_Clear();
        Py_DECREF(capsule);
        return "its DLPack capsule does not name a dltensor";
    }
    if (PyCapsule_SetName(capsule, "used_dltensor") != 0) {
        PyErr_Clear();
        Py_DECREF(capsule);
        return "its DLPack capsule could not be marked consumed";
    }
    try {
        out = camel::core::array::fromDLPack(
            static_cast<const camel::core::array::DLManagedTensor *>(raw),
            camel::core::mm::autoSpace());
    } catch (const std::exception &e) {
        Py_DECREF(capsule);
        return std::string("its DLPack tensor is not one Camel accepts: ") + e.what();
    }
    Py_DECREF(capsule);
    return std::nullopt;
}

/// Converts one Python argument to the slot its declared type asks for.
std::optional<std::string> convertArgument(PyObject *object, Type *declared, Arguments &out) {
    switch (declared->code()) {
    case TypeCode::Bool:
        if (!PyBool_Check(object)) {
            return "expected a bool";
        }
        out.slots.push_back(camel::core::rtdata::toSlot<bool>(object == Py_True));
        return std::nullopt;
    case TypeCode::Int32:
    case TypeCode::Int64: {
        if (!PyLong_Check(object)) {
            return "expected an int";
        }
        const long long value = PyLong_AsLongLong(object);
        if (value == -1 && PyErr_Occurred()) {
            PyErr_Clear();
            return "the int does not fit a 64-bit integer";
        }
        if (declared->code() == TypeCode::Int32) {
            if (value < INT32_MIN || value > INT32_MAX) {
                return "the int does not fit an i32";
            }
            out.slots.push_back(camel::core::rtdata::toSlot<int32_t>(int32_t(value)));
        } else {
            out.slots.push_back(camel::core::rtdata::toSlot<int64_t>(int64_t(value)));
        }
        return std::nullopt;
    }
    case TypeCode::Float32:
    case TypeCode::Float64: {
        if (!PyFloat_Check(object) && !PyLong_Check(object)) {
            return "expected a float";
        }
        const double value = PyFloat_AsDouble(object);
        if (value == -1.0 && PyErr_Occurred()) {
            PyErr_Clear();
            return "the number is not a float";
        }
        if (declared->code() == TypeCode::Float32) {
            out.slots.push_back(camel::core::rtdata::toSlot<float>(float(value)));
        } else {
            out.slots.push_back(camel::core::rtdata::toSlot<double>(value));
        }
        return std::nullopt;
    }
    case TypeCode::String: {
        std::string text;
        if (auto problem = textOf(object, text)) {
            return problem;
        }
        String *value = String::from(text, camel::core::mm::autoSpace());
        out.root(declared, value);
        out.slots.push_back(camel::core::rtdata::toSlot<String *>(value));
        return std::nullopt;
    }
    case TypeCode::Array: {
        auto *declaredArray = static_cast<ArrayType *>(declared);
        Array *array        = nullptr;
        if (auto problem = adoptArray(object, array, "numeric")) {
            return problem;
        }
        Type *element = declaredArray->elemType();
        if (element != nullptr && element->code() != array->dtype()) {
            return "its element type is not the parameter's " +
                   std::string(element->toString());
        }
        // The block of an imported array is external and freed by the producer's deleter; the
        // array itself is a heap object and must be rooted for the call.
        out.root(declared, array);
        out.slots.push_back(camel::core::rtdata::toSlot<Array *>(array));
        return std::nullopt;
    }
    default:
        return "its type " + std::string(declared->toString()) + " is not one this binding carries";
    }
}

// ---- result conversion -----------------------------------------------------

/// The capsule destructor the DLPack protocol prescribes: a consumer that has renamed the capsule
/// to "used_dltensor" has taken the memory over, and this one has not, so it releases it here.
/// `toDLPack` retained the block, and the managed tensor's deleter drops that reference.
void capsuleDestructor(PyObject *capsule) {
    if (PyCapsule_IsValid(capsule, "dltensor")) {
        void *raw = PyCapsule_GetPointer(capsule, "dltensor");
        if (raw != nullptr) {
            auto *managed = static_cast<camel::core::array::DLManagedTensor *>(raw);
            if (managed->deleter != nullptr) {
                managed->deleter(managed);
            }
        } else {
            PyErr_Clear();
        }
    }
}

/// The Python object a returned slot stands for. `type` is the function's declared result type,
/// so an array comes back as a DLPack capsule and a number as a Python number.
PyObject *toPython(slot_t value, Type *type) {
    if (type == nullptr || type->code() == TypeCode::Void) {
        Py_RETURN_NONE;
    }
    if (TupleType *nullable = camel::core::type::asNullable(type)) {
        (void)nullable;
        if (value == NullSlot) {
            Py_RETURN_NONE;
        }
        value = camel::core::rtdata::nullablePayload(value);
        type  = camel::core::type::asNullable(type)->payloadType();
        if (type == nullptr) {
            Py_RETURN_NONE;
        }
    }
    switch (type->code()) {
    case TypeCode::Bool:
        return PyBool_FromLong(camel::core::rtdata::fromSlot<bool>(value) ? 1 : 0);
    case TypeCode::Int32:
        return PyLong_FromLong(camel::core::rtdata::fromSlot<int32_t>(value));
    case TypeCode::Int64:
        return PyLong_FromLongLong(camel::core::rtdata::fromSlot<int64_t>(value));
    case TypeCode::Float32:
        return PyFloat_FromDouble(double(camel::core::rtdata::fromSlot<float>(value)));
    case TypeCode::Float64:
        return PyFloat_FromDouble(camel::core::rtdata::fromSlot<double>(value));
    case TypeCode::String: {
        const auto *text = camel::core::rtdata::fromSlot<String *>(value);
        if (text == nullptr) {
            Py_RETURN_NONE;
        }
        return PyUnicode_FromStringAndSize(text->data(), Py_ssize_t(text->size()));
    }
    case TypeCode::Array: {
        auto *array = camel::core::rtdata::fromSlot<Array *>(value);
        if (array == nullptr) {
            Py_RETURN_NONE;
        }
        if (!array->isCompact() || !camel::core::type::isCompactElementType(array->dtype())) {
            PyErr_SetString(
                PyExc_TypeError,
                "only an array of numeric scalars can be handed to Python through DLPack");
            return nullptr;
        }
        auto *managed = camel::core::array::toDLPack(array);
        PyObject *capsule = PyCapsule_New(managed, "dltensor", &capsuleDestructor);
        if (capsule == nullptr) {
            managed->deleter(managed);
            return nullptr;
        }
        return capsule;
    }
    case TypeCode::Tuple: {
        const auto *tuple     = camel::core::rtdata::fromSlot<Tuple *>(value);
        const auto *tupleType = static_cast<const TupleType *>(type);
        if (tuple == nullptr) {
            Py_RETURN_NONE;
        }
        PyObject *result = PyTuple_New(Py_ssize_t(tupleType->size()));
        if (result == nullptr) {
            return nullptr;
        }
        for (size_t i = 0; i < tupleType->size(); ++i) {
            PyObject *item = toPython(tuple->get<slot_t>(i), tupleType->typeAt(i));
            if (item == nullptr) {
                Py_DECREF(result);
                return nullptr;
            }
            PyTuple_SET_ITEM(result, Py_ssize_t(i), item);
        }
        return result;
    }
    default:
        PyErr_Format(
            PyExc_TypeError, "this binding does not carry a result of type %s",
            type->toString().c_str());
        return nullptr;
    }
}

// ---- reports from the runtime ---------------------------------------------

/// The Error structure of an error value (core/rtdata/tagged.h): kind, message, location.
Report reportOfErrorValue(slot_t fallible) {
    Report r;
    r.fault = true;
    r.stage = "run";
    const auto *info = camel::core::rtdata::fromSlot<const Struct *>(
        camel::core::rtdata::fallibleError(fallible));
    if (info == nullptr) {
        r.name    = "UnhandledError";
        r.message = "the function failed and left no Error structure";
        return r;
    }
    const auto text = [&](std::string_view field) {
        const auto *s = info->get<const String *>(camel::core::type::errorFieldIndex(field));
        return s != nullptr ? s->toString() : std::string();
    };
    r.name    = text("kind");
    r.message = text("message");
    const std::string location = text("location");
    // `file:line:column`, read from the right: a file name may contain a colon. An error an
    // operator made names the operator instead and has no position (entry_args.cpp, the same
    // reading `camel` itself does when an unhandled error reaches the exit of `main`).
    const size_t last = location.rfind(':');
    if (last != std::string::npos && last > 0) {
        const size_t first = location.rfind(':', last - 1);
        if (first != std::string::npos) {
            const auto number = [&](size_t from, size_t to, size_t &out) {
                const char *begin = location.data() + from;
                const char *end   = location.data() + to;
                const auto parsed = std::from_chars(begin, end, out);
                return begin != end && parsed.ec == std::errc() && parsed.ptr == end && out > 0;
            };
            if (number(first + 1, last, r.line) && number(last + 1, location.size(), r.column)) {
                r.file = location.substr(0, first);
            }
        }
    }
    if (r.file.empty() && !location.empty()) {
        r.message += " (made by " + location + ", which records no position)";
    }
    r.text = "error[" + r.name + "]: " + r.message;
    return r;
}

/// A diagnostic the compiler stored, in the parts the exception carries. The rendering is the
/// diagnostic's own (`Diagnostic::toText`), which is what `camel` prints.
Report reportOfDiagnostic(const camel::core::error::Diagnostic &d) {
    Report r;
    r.name       = d.name;
    r.code       = "0x" + camel::core::error::Diagnostic::hex8(d.diagCode());
    r.message    = d.message;
    r.suggestion = d.suggestion;
    r.stage      = d.stage;
    r.file       = camel::core::error::diagnosticPath(d);
    if (const auto range = camel::core::error::resolveSourceRange(d.range, d.sourceContext)) {
        r.line   = range->start.line + 1;
        r.column = range->start.character + 1;
    }
    r.text = d.toText();
    return r;
}

/// The first error of a module that would not compile, with its position; a module that failed
/// without storing one keeps the reason `problem` gave.
Report reportOfLibrary(
    const std::string &problem,
    const std::shared_ptr<camel::core::module::UserDefinedModule> &module) {
    if (module != nullptr && module->diagnostics() != nullptr) {
        const auto errors = module->diagnostics()->errors();
        if (!errors.empty()) {
            return reportOfDiagnostic(*errors.front());
        }
    }
    Report r;
    r.name    = "LibraryUnavailable";
    r.message = problem;
    r.text    = "error: " + problem;
    return r;
}

// ---- the Session type ------------------------------------------------------

struct SessionObject {
    PyObject_HEAD
    std::string entryDir;
    std::vector<std::string> includeDirs;
};

/// `_camel.Session(entry_dir=None, search_dirs=())`.
PyObject *sessionNew(PyTypeObject *type, PyObject *args, PyObject *kwargs) {
    static const char *names[] = {"entry_dir", "search_dirs", nullptr};
    PyObject *entryDirObject   = Py_None;
    PyObject *searchDirsObject = nullptr;
    if (!PyArg_ParseTupleAndKeywords(
            args, kwargs, "|OO", const_cast<char **>(names), &entryDirObject, &searchDirsObject)) {
        return nullptr;
    }
    auto *self = reinterpret_cast<SessionObject *>(type->tp_alloc(type, 0));
    if (self == nullptr) {
        return nullptr;
    }
    new (&self->entryDir) std::string();
    new (&self->includeDirs) std::vector<std::string>();
    if (entryDirObject != Py_None) {
        std::string text;
        if (auto problem = textOf(entryDirObject, text)) {
            PyErr_Format(PyExc_TypeError, "entry_dir: %s", problem->c_str());
            Py_DECREF(self);
            return nullptr;
        }
        self->entryDir = text;
    }
    if (searchDirsObject != nullptr && searchDirsObject != Py_None) {
        PyObject *iterator = PyObject_GetIter(searchDirsObject);
        if (iterator == nullptr) {
            PyErr_SetString(PyExc_TypeError, "search_dirs must be an iterable of str");
            Py_DECREF(self);
            return nullptr;
        }
        PyObject *item = nullptr;
        while ((item = PyIter_Next(iterator)) != nullptr) {
            std::string text;
            if (auto problem = textOf(item, text)) {
                PyErr_Format(PyExc_TypeError, "search_dirs item: %s", problem->c_str());
                Py_DECREF(item);
                Py_DECREF(iterator);
                Py_DECREF(self);
                return nullptr;
            }
            self->includeDirs.push_back(text);
            Py_DECREF(item);
        }
        Py_DECREF(iterator);
        if (PyErr_Occurred()) {
            Py_DECREF(self);
            return nullptr;
        }
    }
    return reinterpret_cast<PyObject *>(self);
}

void sessionDealloc(PyObject *self) {
    auto *session = reinterpret_cast<SessionObject *>(self);
    session->entryDir.~basic_string();
    session->includeDirs.~vector();
    // A type built from a spec is a heap type: releasing an instance releases the type it took a
    // reference on (`tp_alloc`).
    PyTypeObject *type = Py_TYPE(self);
    type->tp_free(self);
    Py_DECREF(type);
}

/// `session.call(library, function, args)` -> the function's value. `args` is a tuple; each item
/// is converted to the declared type of the parameter it stands for.
PyObject *sessionCall(PyObject *self, PyObject *args) {
    auto *session    = reinterpret_cast<SessionObject *>(self);
    const char *library  = nullptr;
    const char *function = nullptr;
    PyObject *arguments  = nullptr;
    if (!PyArg_ParseTuple(args, "ssO", &library, &function, &arguments)) {
        return nullptr;
    }
    if (!PyTuple_Check(arguments)) {
        PyErr_SetString(PyExc_TypeError, "args must be a tuple");
        return nullptr;
    }

    const context_ptr_t context = ensureHost(session->entryDir, session->includeDirs);

    std::string problem;
    std::optional<camel::core::error::DiagLabel> errorLocation;
    camel::runtime::DefinitionStore::LibraryLookup lookup;
    const auto found = context->definitions().libraryFunction(
        library, function, problem, &errorLocation, &lookup);
    if (!found) {
        Report r = reportOfLibrary(problem, lookup.module);
        if (r.line == 0 && errorLocation.has_value()) {
            r.file = errorLocation->path;
            if (const auto range = camel::core::error::resolveSourceRange(
                    errorLocation->range, context->sourceContext())) {
                r.line   = range->start.line + 1;
                r.column = range->start.character + 1;
            }
        }
        // The name says which of the three mistakes it is; the message and the report stay the
        // runtime's own wording (`problem`), which is what a Camel caller is told too.
        if (lookup.absent) {
            r.name = lookup.declaresUnexported ? "FunctionNotExported" : "NoSuchExport";
        }
        CAMEL_RAISE(r);
    }

    camel::runtime::GCGraph *graph = found->graph;
    const FunctionType *signature  = graph->funcType();
    if (signature == nullptr) {
        Report r;
        r.name    = "LibraryFunctionHasNoSignature";
        r.message = "the compiled graph of " + std::string(library) + "::" + function +
                    " has no function type";
        r.text = "error: " + r.message;
        CAMEL_RAISE(r);
    }
    const Py_ssize_t given = PyTuple_GET_SIZE(arguments);
    if (size_t(given) != signature->normTypesCount()) {
        Report r;
        r.name  = "ArgumentCountMismatch";
        r.message = std::string(function) + " takes " +
                    std::to_string(signature->normTypesCount()) + " argument(s), " +
                    std::to_string(given) + " given";
        r.text = "error: " + r.message;
        CAMEL_RAISE(r);
    }

    Arguments converted;
    for (Py_ssize_t i = 0; i < given; ++i) {
        PyObject *item = PyTuple_GET_ITEM(arguments, i);
        Type *declared = signature->normTypeAt(size_t(i));
        if (auto why = convertArgument(item, declared, converted)) {
            Report r;
            r.name    = "ArgumentNotConvertible";
            r.message = "argument " + std::to_string(i + 1) + " of " + function + ": " + *why;
            r.text    = "error: " + r.message;
            CAMEL_RAISE(r);
        }
    }

    // The scheduler runs the function's graph once. Nothing is caught here but the faults of the
    // runtime itself: a fault of the program is a value (`T!`), not an exception, and is read
    // below.
    slot_t result = NullSlot;
    try {
        NodeVMSchedPass vm(context);
        result = vm.callGraph(
            found->program, graph, std::span<const slot_t>(converted.slots.data(),
                                                            converted.slots.size()));
    } catch (const camel::core::error::RuntimeFault &fault) {
        // The same construction src/execute/standalone_bridge.cpp uses for a fault of an
        // operator: the catalogue entry plus the ten strings the format has.
        std::array<std::string, 10> a;
        for (size_t i = 0; i < fault.formatArgs().size() && i < a.size(); ++i) {
            a[i] = fault.formatArgs()[i];
        }
        const auto rendered = camel::core::error::DiagnosticBuilder::of(fault.diag(), nullptr)
                                  .commit(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
        Report r;
        r.fault      = true;
        r.stage      = "run";
        r.name       = rendered.name;
        r.code       = "0x" + camel::core::error::Diagnostic::hex8(rendered.diagCode());
        r.suggestion = rendered.suggestion;
        r.message    = rendered.message;
        r.text       = rendered.toText();
        CAMEL_RAISE(r);
    } catch (const camel::core::error::Diagnostic &diagnostic) {
        CAMEL_RAISE(reportOfDiagnostic(diagnostic));
    } catch (const std::exception &e) {
        Report r;
        r.fault   = true;
        r.stage   = "run";
        r.name    = "HostFailure";
        r.message = e.what();
        r.text    = std::string("error: ") + e.what();
        CAMEL_RAISE(r);
    }

    Type *resultType = signature->hasExitType() ? signature->exitType() : nullptr;
    if (TupleType *fallible = camel::core::type::asFallible(resultType)) {
        if (camel::core::rtdata::isErrorValue(result)) {
            CAMEL_RAISE(reportOfErrorValue(result));
        }
        result     = camel::core::rtdata::falliblePayload(result);
        resultType = fallible->payloadType();
    }
    // The result is a heap object that the next Camel safepoint may move: root it until the
    // Python object holding its data exists.
    std::optional<RootHandle> resultRoot;
    if (result != NullSlot && resultType != nullptr &&
        (resultType->code() == TypeCode::Array || resultType->code() == TypeCode::String)) {
        resultRoot.emplace(
            camel::core::mm::autoSpace(),
            reinterpret_cast<camel::core::rtdata::Object *>(result), resultType, "python result");
    }
    return toPython(result, resultType);
}

PyMethodDef sessionMethods[] = {
    {"call", sessionCall, METH_VARARGS,
     "call(library, function, args) -> object\n\n"
     "Runs an exported, non-generic function of a Camel module and gives its value."},
    {nullptr, nullptr, 0, nullptr},
};

/// The type is built from a spec (`PyType_FromSpec`) rather than a filled-in `PyTypeObject`: the
/// struct has more than fifty positional fields, and naming only the five slots this type uses is
/// how the header itself does it (Include/object.h).
PyType_Slot sessionSlots[] = {
    {Py_tp_new, reinterpret_cast<void *>(sessionNew)},
    {Py_tp_dealloc, reinterpret_cast<void *>(sessionDealloc)},
    {Py_tp_methods, sessionMethods},
    {Py_tp_doc, const_cast<char *>("One Camel runtime: a context, its module search paths and "
                                   "the libraries it has compiled.")},
    {0, nullptr},
};

PyType_Spec sessionSpec = {
    "camel._camel.Session", sizeof(SessionObject), 0, Py_TPFLAGS_DEFAULT, sessionSlots};

// ---- the module ------------------------------------------------------------

PyMethodDef moduleMethods[] = {
    {nullptr, nullptr, 0, nullptr},
};

struct PyModuleDef camelModule = {
    PyModuleDef_HEAD_INIT,
    "_camel",
    "The Camel runtime behind `import camel`.",
    -1,
    moduleMethods,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
};

} // namespace

PyMODINIT_FUNC PyInit__camel(void) {
    PyObject *sessionType = PyType_FromSpec(&sessionSpec);
    if (sessionType == nullptr) {
        return nullptr;
    }

    PyObject *module = PyModule_Create(&camelModule);
    if (module == nullptr) {
        Py_DECREF(sessionType);
        return nullptr;
    }
    if (PyModule_AddObjectRef(module, "Session", sessionType) < 0) {
        Py_DECREF(sessionType);
        Py_DECREF(module);
        return nullptr;
    }
    Py_DECREF(sessionType);

    CamelErrorType = PyErr_NewExceptionWithDoc(
        "camel.CamelError",
        "Base class of everything this binding raises: the parts are attributes and the rendered "
        "report is str().",
        nullptr, nullptr);
    DiagnosticErrorType = PyErr_NewExceptionWithDoc(
        "camel.DiagnosticError",
        "A diagnostic of the compiler, carrying name, code, message, suggestion, stage and the "
        "position file/line/column.",
        CamelErrorType, nullptr);
    FaultErrorType = PyErr_NewExceptionWithDoc(
        "camel.FaultError",
        "A fault of the run, carrying the kind, the message and, when the error value recorded "
        "one, the position file/line/column.",
        CamelErrorType, nullptr);
    if (CamelErrorType == nullptr || DiagnosticErrorType == nullptr || FaultErrorType == nullptr) {
        Py_DECREF(module);
        return nullptr;
    }
    if (PyModule_AddObjectRef(module, "CamelError", CamelErrorType) < 0 ||
        PyModule_AddObjectRef(module, "DiagnosticError", DiagnosticErrorType) < 0 ||
        PyModule_AddObjectRef(module, "FaultError", FaultErrorType) < 0) {
        Py_DECREF(module);
        return nullptr;
    }
    return module;
}