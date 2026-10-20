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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The exact value of a literal, and of what literals compute to (design principles 5.1):
 *
 *   A literal takes its type from its context (the expected type, the other operand of a
 *   binary operator, the default type). Until that type is known the value is kept exactly:
 *   `0.1` is one tenth, not the double nearest to it, so `let x: f32 = 0.1` is the f32
 *   nearest to one tenth. Arithmetic between literals is folded here, exactly, with no
 *   intermediate overflow and no rounding before the target type is known; a value that the
 *   target type cannot hold is a compile-time error (the compiler reports it; this file only
 *   says that it does not fit).
 *
 * The value is an arbitrary-precision rational: integer literals are integers, and a decimal
 * or exponent form (`0.1`, `1.5e-3`) is the exact decimal that was written. Division between
 * two integer literals is the integer division the language defines (it truncates); with a
 * float operand the quotient is the exact rational, rounded only when it becomes a value of
 * the target type.
 */

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "camel/core/data.h"
#include "camel/core/type.h"

namespace camel::compile::literal {

enum class Kind : std::uint8_t {
    Integer,
    Float,
    /// A literal written with an `i` suffix (`4i`): its imaginary part is the value, its real
    /// part is zero.
    Imaginary,
    /// A value with both parts, from folding an expression that has an imaginary operand.
    Complex,
};

class Value {
  public:
    Value();
    Value(const Value &);
    Value &operator=(const Value &);
    ~Value();

    static Value fromInt64(std::int64_t value);
    /// The exact value of a double, used when a literal is read back from a value.
    static Value fromDouble(double value);
    /// The value of the integer literal `text` (with its sign and its base prefix).
    static std::optional<Value> fromIntegerText(std::string_view text);
    /// The value of the real literal `text` (with its sign), exactly as written.
    static std::optional<Value> fromRealText(std::string_view text);
    /// The value of the imaginary literal `text` (`4i`, `-0.5i`): the body is read as a real,
    /// its value becomes the imaginary part and the real part is zero.
    static std::optional<Value> fromImaginaryText(std::string_view text);

    Kind kind() const { return kind_; }
    /// True for a value with a nonzero imaginary part (or an imaginary literal of zero).
    bool hasImaginaryPart() const;
    bool isZero() const;
    bool isNegative() const;

    /// The value as text: its exact decimal when it has one, a fraction (`1/3`) when it has
    /// none, and scientific notation when the exact decimal would be very long.
    std::string toString() const;
    /// Text that reads back as this value in a floating-point type: the exact decimal when it
    /// has one, otherwise 50 significant digits of the quotient.
    std::string toFloatText() const;

    /// The value as a constant of a numeric primitive type. An integer target takes an
    /// integer value only, and only one that fits. A float target takes either kind, rounded
    /// to nearest with ties to even; a magnitude below the smallest subnormal becomes zero.
    /// Null when the value is not one the type can hold.
    std::optional<camel::core::data::data_ptr_t> toData(camel::core::type::Type *type) const;
    /// Integer kind and fitting an int64.
    std::optional<std::int64_t> asInt64() const;

    // Exact arithmetic. Null when the operation has no value here: division or a remainder by
    // zero (left to run time, as before), a power that would run away, or an operation the
    // kinds do not define (a remainder with a float operand). `div` is integer division when
    // both values are integers.
    static std::optional<Value> add(const Value &left, const Value &right);
    static std::optional<Value> sub(const Value &left, const Value &right);
    static std::optional<Value> mul(const Value &left, const Value &right);
    static std::optional<Value> div(const Value &left, const Value &right);
    static std::optional<Value> mod(const Value &left, const Value &right);
    static std::optional<Value> pow(const Value &base, const Value &exponent);
    static std::optional<Value> negate(const Value &value);
    /// -1, 0 or 1 as this value is below, equal to or above `other`.
    int compare(const Value &other) const;

  /// The representation of an exact value: its real part and its imaginary part. Public so that
    /// the folding helpers in literal.cpp can build one; a Value is still immutable.
    struct Rep;
    Value(Kind kind, std::shared_ptr<const Rep> rep);

  private:
    Kind kind_ = Kind::Integer;
    std::shared_ptr<const Rep> rep_;
};

} // namespace camel::compile::literal

namespace camel::compile::literal {

/// Whether a value of this kind could be a value of `type` at all: an integer literal can be
/// an integer or a float, a float literal only a float. Whether it fits the type's range is
/// a separate question (`Value::toData`).
bool kindEnters(const Value &value, camel::core::type::Type *type);

/// The range of a numeric primitive type as a diagnostic says it ("-2147483648 to 2147483647",
/// "at most about 3.4e38 in magnitude"); empty for a type that is not one of them.
std::string rangeText(camel::core::type::Type *type);

} // namespace camel::compile::literal