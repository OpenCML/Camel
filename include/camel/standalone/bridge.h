// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The boundary between a program std::cpp wrote and the Camel runtime, for the operators that
 * have no standalone translation (docs/design/cpp-standalone/README.cn.md, section 9). Only a
 * program that uses one includes this header, and it links libcamel; every other program is
 * standalone.
 *
 * The boundary is one function, bridge::call, implemented in libcamel
 * (src/execute/standalone_bridge.cpp). What crosses it is plain C++: the arguments and the
 * result as Values (numbers, booleans, the bytes of a string, the elements of an array with its
 * shape), a fault as the texts of its diagnostic. A value of a type the program has of its own
 * crosses as that value: a tensor is a `T[..]`, the shaped array `Array` is (docs/design/cpp-standalone/README.cn.md 4), so
 * the bridge converts it at the boundary -- the elements copied out of the runtime's value, its
 * shape with them -- and the program prints it as it prints any array of that type. Only an object
 * that is opaque in the language itself (a `PyObject`, a `Device<T>`) crosses as a counted handle
 * (Foreign): a program could not read it if it held it. The other runtime objects exist only while
 * the operator runs, inside that function, and this header includes nothing of the runtime.
 */

#pragma once

#include "camel/standalone/array.h"
#include "camel/standalone/core.h"
#include "camel/standalone/foreign.h"
#include "camel/standalone/str.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace camel::standalone::bridge {

/// A value handed to or returned by an operator of the runtime.
struct Value {
    enum class Kind : uint8_t {
        Void,
        Bool,
        Int32,
        Int64,
        Float32,
        Float64,
        String,
        IntArray,
        Array,
        Foreign
    };
    Kind kind       = Kind::Void;
    int64_t integer = 0; // Bool, Int32, Int64
    double real     = 0; // Float32, Float64
    std::string text;    // String
    std::vector<int64_t> ints; // IntArray
    void *foreign = nullptr;   // Foreign
    /// Array: the elements of a value the runtime built, copied out as the bytes of the element
    /// type the value states, and its extent table -- a tensor is a `T[..]` like any other array.
    std::vector<std::byte> bytes;
    std::vector<int64_t> extents;
};

/// What the runtime's diagnostic says about a fault an operator raised.
struct OperatorFault {
    std::string name;       // RuntimeDiag::...
    std::string code;       // 0x...
    std::string message;
    std::string suggestion;
};

/// Runs the operator `uri` of the runtime on `norm` and `with` and gives its result as the kind
/// `result` names: a scalar as its value, an array (a tensor included, `Array`) as its elements
/// copied out and the extent table they make. `resultElemCode` is the element type of the call's
/// result as the integer code of the type, for an operator whose result is a value the runtime
/// builds and whose kernel reads the call's result type (`TypeCode`, e.g. `tensor:ones` fills the
/// element type the call states); -1 means the call states no result type and the runtime reads its
/// own, as it did before a code could cross. The runtime is started, and the operator's module
/// loaded, at the first call. Throws OperatorFault when the operator faults. Implemented in
/// libcamel.
Value call(
    std::string_view uri, const std::vector<Value> &norm, const std::vector<Value> &with,
    Value::Kind result, int64_t resultElemCode = -1);

inline Value arg(bool v) { return {Value::Kind::Bool, v ? 1 : 0, 0, {}}; }
inline Value arg(int32_t v) { return {Value::Kind::Int32, v, 0, {}}; }
inline Value arg(int64_t v) { return {Value::Kind::Int64, v, 0, {}}; }
inline Value arg(float v) { return {Value::Kind::Float32, 0, v, {}}; }
inline Value arg(double v) { return {Value::Kind::Float64, 0, v, {}}; }
inline Value arg(const Str &v) {
    return {Value::Kind::String, 0, 0, v.str(), {}, nullptr, {}, {}};
}

/// A shape or any other array of integers, as an operator argument: a value, not a constant, so a
/// shape computed at run time crosses as well as a literal one.
inline Value arg(const std::vector<int64_t> &v) {
    Value out;
    out.kind = Value::Kind::IntArray;
    out.ints = v;
    return out;
}

/// The same for an `int[]` of the generated program, whose elements lie flat in row order: the
/// runtime's arrays of integers are one run of elements too, so a shape built by the program
/// (`[n, 3]` with `n` computed, `reshape`'s target) crosses as the array it is.
inline Value arg(const Array<int64_t> &v) {
    Value out;
    out.kind = Value::Kind::IntArray;
    out.ints.reserve(v.size());
    for (std::size_t i = 0; i < v.size(); ++i) {
        out.ints.push_back(v[i]);
    }
    return out;
}

/// A handle to a value the runtime owns (foreign.h): the objects the language itself keeps opaque
/// (`PyObject`, `Device<T>`), passed on as they are, the count taken by hand.
inline Value arg(const Foreign &v) {
    Value out;
    out.kind    = Value::Kind::Foreign;
    out.foreign = Foreign::retain(v.handle());
    return out;
}

/// `Array<T>` as the bridge carries it: the elements of one type and the shape they make.
template <typename T> struct ElementOf {
    static constexpr bool array = false;
};

template <typename T> struct ElementOf<Array<T>> {
    static constexpr bool array = true;
    using type                  = T;
};

template <typename T> constexpr bool isArray = ElementOf<T>::array;

template <typename T> constexpr Value::Kind kindOf() {
    if constexpr (std::is_same_v<T, void>) {
        return Value::Kind::Void;
    } else if constexpr (std::is_same_v<T, bool>) {
        return Value::Kind::Bool;
    } else if constexpr (std::is_same_v<T, int32_t>) {
        return Value::Kind::Int32;
    } else if constexpr (std::is_same_v<T, int64_t>) {
        return Value::Kind::Int64;
    } else if constexpr (std::is_same_v<T, float>) {
        return Value::Kind::Float32;
    } else if constexpr (std::is_same_v<T, double>) {
        return Value::Kind::Float64;
    } else if constexpr (isArray<T>) {
        // `T[]` of whatever shape: a tensor is a `T[..]` too, and it crosses as the array it is,
        // converted at the boundary rather than handed over as a handle.
        return Value::Kind::Array;
    } else if constexpr (std::is_same_v<T, Foreign>) {
        // An object the language itself keeps opaque, which crosses as the handle to it rather
        // than as its value.
        return Value::Kind::Foreign;
    } else {
        static_assert(std::is_same_v<T, Str>, "a type the bridge does not carry");
        return Value::Kind::String;
    }
}

/// An operator of the runtime, as the generated code calls it.
class Operator {
  public:
    explicit constexpr Operator(const char *uri) : uri_(uri) {}

    /// The call at `place` (whose report a fault of the operator uses). `resultElemCode` is the
    /// call's result element type as a `TypeCode`, for the operators that need to be told it
    /// (bridge::call); -1, the default, states none.
    template <typename R>
    R call(const Place &place, const std::vector<Value> &norm,
           const std::vector<Value> &with, int64_t resultElemCode = -1) const {
        Value v;
        try {
            v = bridge::call(uri_, norm, with, kindOf<R>(), resultElemCode);
        } catch (const OperatorFault &f) {
            // The report as `camel` renders it: the message line, the excerpt, the help line;
            // and the one-line form.
            std::string text = "error[" + f.name + "]: " + f.message + "\n" + place.excerpt;
            if (!f.suggestion.empty()) {
                text += "\nhelp: " + f.suggestion;
            }
            std::string brief = std::string(place.where) + ": [Error]: " + f.message + " " +
                                f.suggestion + " (name=" + f.name + ", code=" + f.code + ")";
            std::string kind = f.name.substr(f.name.rfind(':') + 1);
            throw Fault(Fault::Texts{
                std::move(text), std::move(brief), std::move(kind), f.message, place.location});
        }
        if constexpr (std::is_same_v<R, void>) {
            return;
        } else if constexpr (std::is_same_v<R, Str>) {
            return Str(v.text);
        } else if constexpr (isArray<R>) {
            // The value the runtime built, as the array the program holds: the elements it copied
            // out, the shape the value states (`Array::fromFlat`). A tensor is this value and no
            // other -- one representation per type, and it is the type's, not the operator's. The
            // elements are the type the call stated, and a call and its kernel agree on that
            // (`resultElemCode`), so `bytes` holds whole elements of it.
            using Elem              = typename ElementOf<R>::type;
            const std::size_t count = v.bytes.size() / sizeof(Elem);
            std::vector<Elem> items(count);
            if (count != 0) {
                std::memcpy(items.data(), v.bytes.data(), count * sizeof(Elem));
            }
            return R::fromFlat(std::move(items), v.extents);
        } else if constexpr (std::is_same_v<R, Foreign>) {
            // The handle the bridge made owns one count, and the Foreign that reaches the program
            // is the one that takes it: no count is added here (foreign.h).
            return Foreign(v.foreign);
        } else if constexpr (std::is_floating_point_v<R>) {
            return static_cast<R>(v.real);
        } else {
            return static_cast<R>(v.integer);
        }
    }

  private:
    const char *uri_;
};

} // namespace camel::standalone::bridge
