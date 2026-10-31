// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The runtime's side of camel/standalone/bridge.h: a program std::cpp wrote calls an operator
 * that has no standalone translation through bridge::call, which is this file.
 *
 * The first call creates a Context the way the command-line tool does (module search paths
 * from CAMEL_HOME and the directory of the executable). Each call turns the arguments into
 * slots (a string into a String of the runtime's heap), runs the operator, and turns the result
 * back -- an array, and a tensor is one, as its elements copied out with the shape the value
 * states. The runtime's objects live only during the call; no safepoint is reached in it (an
 * operator does not run Camel code), so nothing has to be rooted.
 */

#include "camel/standalone/bridge.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/type.h"
#include "camel/execute/executor.h"
#include "camel/utils/dll_path.h"
#include "camel/utils/install_layout.h"

#include <array>
#include <filesystem>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace camel::standalone::bridge {

namespace {

using camel::core::rtdata::fromSlot;
using camel::core::rtdata::toSlot;
using camel::core::type::ArrayType;
using camel::core::type::Type;
using camel::core::type::TypeCode;

/// Arguments of one call: slots and their types.
class ArgsOfCall final : public ::ArgsView {
  public:
    explicit ArgsOfCall(const std::vector<Value> &values) {
        for (const Value &v : values) {
            switch (v.kind) {
            case Value::Kind::Bool:
                slots_.push_back(toSlot<bool>(v.integer != 0));
                types_.push_back(Type::Bool());
                break;
            case Value::Kind::Int32:
                slots_.push_back(toSlot<int32_t>(static_cast<int32_t>(v.integer)));
                types_.push_back(Type::Int32());
                break;
            case Value::Kind::Int64:
                slots_.push_back(toSlot<int64_t>(v.integer));
                types_.push_back(Type::Int64());
                break;
            case Value::Kind::Float32:
                slots_.push_back(toSlot<float>(static_cast<float>(v.real)));
                types_.push_back(Type::Float32());
                break;
            case Value::Kind::Float64:
                slots_.push_back(toSlot<double>(v.real));
                types_.push_back(Type::Float64());
                break;
            case Value::Kind::String:
                slots_.push_back(toSlot(String::from(v.text, camel::core::mm::autoSpace())));
                types_.push_back(Type::String());
                break;
case Value::Kind::IntArray: {
                // A shape (or any other int array) as the runtime holds it: an `int[]`. It is built
                // here rather than in the generated code so the program never needs the runtime's
                // value layout -- the array is the runtime's own, made with its own allocator.
                // `::Array` spelled out: bridge.h brings in camel::standalone::Array, the program's
                // array, which is another type under the same name.
                ArrayType *arrayType = ArrayType::create(Type::Int64());
                ::Array *array =
                    ::Array::create(camel::core::mm::autoSpace(), arrayType, v.ints.size());
                for (size_t i = 0; i < v.ints.size(); ++i) {
                    array->set<camel::core::rtdata::Int64>(i, v.ints[i]);
                }
                slots_.push_back(toSlot<::Array *>(array));
                types_.push_back(arrayType);
                break;
            }
            case Value::Kind::Array: {
                // A value that came back through the bridge, handed to another operator of it. No
                // rule does that yet -- every array the tree builds is a result -- and passing on
                // bytes no slot holds would be worse than saying so.
                throw camel::core::error::RuntimeFault::make(
                    camel::core::error::RuntimeDiag::PassInternalError,
                    "std::cpp",
                    "an array that came back through the bridge was passed to an operator that "
                    "takes one, which the bridge does not do yet");
            }
            case Value::Kind::Void:
                slots_.push_back(NullSlot);
                types_.push_back(Type::Void());
                break;
            }
        }
    }

    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    TypeCode code(size_t index) const override { return types_[index]->code(); }
    Type *type(size_t index) const override { return types_[index]; }

  private:
    std::vector<slot_t> slots_;
    std::vector<Type *> types_;
};

struct Runtime {
    camel::core::context::context_ptr_t context;
    std::unordered_set<std::string> imported;
    std::unordered_map<std::string, ::operator_t> operators;
};

Runtime &runtime() {
    static Runtime rt = [] {
        camel::utils::setupLibrarySearchPathForApp();
        auto dir = camel::utils::getExecutableDirectory();
        if (dir.empty()) {
            dir = std::filesystem::current_path();
        }
        Runtime r;
        r.context = camel::core::context::Context::create(
            camel::core::context::EntryConfig{
                .entryDir    = camel::utils::pathToUtf8(dir),
                .entryFile   = "",
                .searchPaths = camel::utils::buildModuleSearchPaths(camel::utils::pathToUtf8(dir)),
            },
            camel::core::error::DiagsConfig{});
        return r;
    }();
    return rt;
}

/// The texts of the diagnostic a fault of the runtime has.
OperatorFault describe(const camel::core::error::RuntimeFault &fault) {
    std::array<std::string, 10> a;
    for (size_t i = 0; i < fault.formatArgs().size() && i < a.size(); ++i) {
        a[i] = fault.formatArgs()[i];
    }
    const auto d = camel::core::error::DiagnosticBuilder::of(fault.diag(), nullptr)
                       .commit(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
    // A fault the program raised with `fail<'Kind'>` is reported under the kind it wrote, not
    // under the catalogue entry it travels as (design principles 3.9, RuntimeFault::kind): the
    // VM renders it that way, and a reader comparing the two paths must not see two names for
    // one fault.
    const std::string name =
        fault.kind().empty() ? d.name : std::string("RuntimeDiag::") + fault.kind();
    return {name, "0x" + camel::core::error::Diagnostic::hex8(d.diagCode()), d.message, d.suggestion};
}

::operator_t operatorFor(std::string_view uri) {
    Runtime &r = runtime();
    std::string key(uri);
    if (auto it = r.operators.find(key); it != r.operators.end()) {
        return it->second;
    }
    // "fs:read_text" is an operator of the module fs: load it once.
    if (const size_t colon = key.find(':'); colon != std::string::npos && colon > 0) {
        const std::string module = key.substr(0, colon);
        if (r.imported.insert(module).second) {
            r.context->importModule(module);
        }
    }
    auto op = r.context->execMgr().find(key);
    if (!op) {
        // The generated program names an operator its modules do not provide: the translation
        // and the runtime disagree, which is a defect of the compiler, not of the program.
        throw describe(camel::core::error::RuntimeFault::make(
            camel::core::error::RuntimeDiag::PassInternalError,
            "std::cpp",
            "the operator '" + key + "' is not provided by the module that declares it"));
    }
    r.operators.emplace(key, *op);
    return *op;
}

/// The type a value the runtime owns is printed by. A tensor is a numeric array
/// (modules/tensor/tensor.h): its element type and its shape are the value's own, so the type is
/// the array type they make -- the reading modules/tensor/module.cpp's value refiner does, and the
/// one every reader of the slot (the printer included) takes a tensor for. A value that is not an
/// array states no type this side can read, and the printer falls back to the object's own.
const Type *typeOfValue(camel::core::rtdata::Object *object) {
    auto *array = dynamic_cast<::Array *>(object);
    if (array == nullptr) {
        return nullptr;
    }
    const auto dims = array->shapeSpan();
    Type *elem      = Type::create(array->elemTypeCode());
    return dims.empty() ? elem : ArrayType::create(elem, dims);
}

/// The value an operator returned, as the program holds it: an array -- and a tensor is one, a
/// `T[..]` like any other (modules/tensor/tensor.h) -- as its elements copied out of the runtime's
/// value and the extent table that value states.
///
/// The elements of a compact array of pure data are the bytes of the element type itself, and that
/// type's bits are the program's too: `float`, `double`, `int32_t`, `int64_t`, `bool`, the two
/// sixteen-bit halves (camel/standalone/half.h writes the pattern camel/core/rtdata/half.h writes)
/// and the complexes make the same run of bytes on either side. So the run is copied as it lies,
/// which is the copy this conversion costs and accepts. An element the runtime traces (a string, an
/// array, a struct) has no such bytes, and a view stores its elements behind an index table: no
/// bridged operator returns either today, and a value of one is a defect of the runtime's table
/// rather than something to read wrong.
Value arrayValue(::Array *array) {
    Value out;
    out.kind = Value::Kind::Array;
    if (array == nullptr) {
        return out;
    }
    const TypeCode elem = array->elemTypeCode();
    if (!array->isCompact() || array->isAggregateElement() || elem == TypeCode::Void ||
        camel::core::type::isGCTraced(elem)) {
        throw camel::core::error::RuntimeFault::make(
            camel::core::error::RuntimeDiag::PassInternalError,
            "std::cpp",
            "the operator returned a value this back end cannot hand over as an array: it is a view "
            "or an element type the bridge does not copy");
    }
    const auto dims = array->shapeSpan();
    out.extents.assign(dims.begin(), dims.end());
    out.bytes.resize(array->byteSize());
    if (!out.bytes.empty()) {
        std::memcpy(out.bytes.data(), array->rawData(), out.bytes.size());
    }
    return out;
}

Value valueOf(slot_t slot, Value::Kind kind) {
    switch (kind) {
    case Value::Kind::Bool:
        return {kind, fromSlot<bool>(slot) ? 1 : 0, 0, {}};
    case Value::Kind::Int32:
        return {kind, fromSlot<int32_t>(slot), 0, {}};
    case Value::Kind::Int64:
        return {kind, fromSlot<int64_t>(slot), 0, {}};
    case Value::Kind::Float32:
        return {kind, 0, fromSlot<float>(slot), {}};
    case Value::Kind::Float64:
        return {kind, 0, fromSlot<double>(slot), {}};
    case Value::Kind::String: {
        const auto *s = fromSlot<String *>(slot);
        return {kind, 0, 0, s != nullptr ? std::string(s->view()) : std::string()};
    }
    case Value::Kind::Array:
        // A tensor the operator built, as the array the program asked for: the elements copied
        // out, the shape with them (arrayValue). The program prints it as it prints an array of
        // the same type that it built itself.
        return arrayValue(fromSlot<::Array *>(slot));
    case Value::Kind::Foreign: {
        // An object the language itself keeps opaque, handed to the generated program as the
        // counted handle of camel/standalone/foreign.h: the handle is made here, inside the call (no
        // safepoint is reached in one), and the Foreign that reaches the program owns its one
        // count. The runtime keeps the object alive from now on, and the text of it is the
        // runtime's own.
        auto *object = fromSlot<camel::core::rtdata::Object *>(slot);
        auto *root   = new camel::core::mm::RootHandle(
            camel::core::mm::autoSpace(), object, typeOfValue(object), "bridge Foreign");
        return {kind, 0, 0, {}, {}, root, {}, {}};
    }
    case Value::Kind::Void:
    default:
        return {};
    }
}

} // namespace

Value call(
    std::string_view uri, const std::vector<Value> &norm, const std::vector<Value> &with,
    Value::Kind result, int64_t resultElemCode) {
    ::operator_t op = operatorFor(uri);
    try {
        ArgsOfCall normArgs(norm);
        ArgsOfCall withArgs(with);
        if (resultElemCode >= 0) {
            // The call's result type, which a kernel that builds a value reads to know what to
            // build: `tensor:ones` fills the element type the call states, read as the element of
            // its result type (modules/tensor/ops/creation_ops.cpp, resultElement), so the type of
            // the element alone would say nothing. What crosses is the element's code: the Type is
            // made from it here (Type::create), never parsed from a text of the type, and the
            // extents are the call's own business -- `T[..]` is the result type such a call
            // declares (stdlib/tensor.cml).
            auto *element = camel::core::type::Type::create(
                static_cast<camel::core::type::TypeCode>(resultElemCode));
            auto *resultType = camel::core::type::ArrayType::openRank(element);
            normArgs.setResultType(resultType);
            withArgs.setResultType(resultType);
        }
        const slot_t out = op(withArgs, normArgs, *runtime().context);
        return valueOf(out, result);
    } catch (const camel::core::error::RuntimeFault &fault) {
        throw describe(fault);
    } catch (const camel::core::error::Diagnostic &d) {
        throw OperatorFault{
            d.name,
            "0x" + camel::core::error::Diagnostic::hex8(d.diagCode()),
            d.message,
            d.suggestion};
    }
}

} // namespace camel::standalone::bridge
