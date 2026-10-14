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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The boundary between a program std::cpp wrote and the Camel runtime, for the operators that
 * have no standalone translation (docs/design/cpp-standalone/README.cn.md, section 9). Only a
 * program that uses one includes this header, and it links libcamel; every other program is
 * standalone.
 *
 * The boundary is one function, bridge::call, implemented in libcamel
 * (src/execute/standalone_bridge.cpp). What crosses it is plain C++: the arguments and the
 * result as Values (numbers, booleans, the bytes of a string), a fault as the texts of its
 * diagnostic. The runtime's objects exist only while the operator runs, inside that function;
 * the generated code never holds one, and this header includes nothing of the runtime.
 */

#pragma once

#include "camel/standalone/core.h"
#include "camel/standalone/str.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace camel::standalone::bridge {

/// A value handed to or returned by an operator of the runtime.
struct Value {
    enum class Kind : uint8_t { Void, Bool, Int32, Int64, Float32, Float64, String };
    Kind kind       = Kind::Void;
    int64_t integer = 0; // Bool, Int32, Int64
    double real     = 0; // Float32, Float64
    std::string text;    // String
};

/// What the runtime's diagnostic says about a fault an operator raised.
struct OperatorFault {
    std::string name;       // RuntimeDiag::...
    std::string code;       // 0x...
    std::string message;
    std::string suggestion;
};

/// Runs the operator `uri` of the runtime on `norm` and `with` and gives its result as
/// `result`. The runtime is started, and the operator's module loaded, at the first call.
/// Throws OperatorFault when the operator faults. Implemented in libcamel.
Value call(
    std::string_view uri, const std::vector<Value> &norm, const std::vector<Value> &with,
    Value::Kind result);

inline Value arg(bool v) { return {Value::Kind::Bool, v ? 1 : 0, 0, {}}; }
inline Value arg(int32_t v) { return {Value::Kind::Int32, v, 0, {}}; }
inline Value arg(int64_t v) { return {Value::Kind::Int64, v, 0, {}}; }
inline Value arg(float v) { return {Value::Kind::Float32, 0, v, {}}; }
inline Value arg(double v) { return {Value::Kind::Float64, 0, v, {}}; }
inline Value arg(const Str &v) { return {Value::Kind::String, 0, 0, v.str()}; }

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
    } else {
        static_assert(std::is_same_v<T, Str>, "a type the bridge does not carry");
        return Value::Kind::String;
    }
}

/// An operator of the runtime, as the generated code calls it.
class Operator {
  public:
    explicit constexpr Operator(const char *uri) : uri_(uri) {}

    /// The call at `place` (whose report a fault of the operator uses).
    template <typename R>
    R call(const Place &place, const std::vector<Value> &norm,
           const std::vector<Value> &with) const {
        Value v;
        try {
            v = bridge::call(uri_, norm, with, kindOf<R>());
        } catch (const OperatorFault &f) {
            // The report as `camel` renders it: the message line, the excerpt, the help line;
            // and the one-line form.
            std::string text = "error[" + f.name + "]: " + f.message + "\n" + place.excerpt;
            if (!f.suggestion.empty()) {
                text += "\nhelp: " + f.suggestion;
            }
            std::string brief = std::string(place.where) + ": [Error]: " + f.message + " " +
                                f.suggestion + " (name=" + f.name + ", code=" + f.code + ")";
            throw Fault(std::move(text), std::move(brief));
        }
        if constexpr (std::is_same_v<R, void>) {
            return;
        } else if constexpr (std::is_same_v<R, Str>) {
            return Str(v.text);
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
