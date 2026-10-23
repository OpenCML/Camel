// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "ops.h"
#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/assert.h"
#include "camel/utils/int_div.h"
#include "camel/utils/utf8.h"

#include "half_ops.h"

#include <cmath>
#include <complex>
#include <limits>

#include "camel/core/error/runtime.h"

namespace mm = camel::core::mm;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

// A `var` variable is a cell: a one-slot tuple (TupleType::cell) that the declaration creates
// and that every `var` parameter and capture bound to the variable points to. Reading the
// variable and assigning to it are the operators below, applied to the cell, so a store is
// seen through every reference.
namespace {
Tuple *variableOf(ArgsView &norm) {
    Tuple *cell = norm.get<Tuple *>(0);
    ASSERT(cell != nullptr && cell->size() == 1, "Operand is not a variable.");
    return cell;
}

// ---- Complex numbers (c64, c128) ---------------------------------------------------------
//
// The arithmetic is the one of the two IEEE parts in the width of the value. A c64 operand is
// kept in an object whose parts are exactly f32 values, so the float specialization computes
// the same bits as an f32 operation. Division by the complex zero is a fault, like a float
// division by zero (design principles 5.1).

template <typename T> std::complex<T> asComplex(const Complex *value);

template <> inline std::complex<float> asComplex(const Complex *value) {
    return {value->realF(), value->imaginaryF()};
}

template <> inline std::complex<double> asComplex(const Complex *value) {
    return {value->real(), value->imaginary()};
}

enum class ComplexOp { Add, Sub, Mul, Div };

template <typename T>
std::complex<T> complexCompute(const Complex *left, const Complex *right, ComplexOp op) {
    const std::complex<T> a = asComplex<T>(left);
    const std::complex<T> b = asComplex<T>(right);
    switch (op) {
    case ComplexOp::Add:
        return a + b;
    case ComplexOp::Sub:
        return a - b;
    case ComplexOp::Mul:
        return a * b;
    case ComplexOp::Div:
        if (b == std::complex<T>{}) {
            throwRuntimeFault(RuntimeDiag::DivisionByZero);
        }
        return a / b;
    }
    return {};
}

template <typename T> slot_t complexBinary(ArgsView &norm, TypeCode code, ComplexOp op) {
    const auto result = complexCompute<T>(norm.get<Complex *>(0), norm.get<Complex *>(1), op);
    return toSlot(
        Complex::create(
            static_cast<double>(result.real()),
            static_cast<double>(result.imag()),
            code,
            mm::autoSpace()));
}

template <typename T> slot_t complexNegate(ArgsView &norm, TypeCode code) {
    const std::complex<T> value = asComplex<T>(norm.get<Complex *>(0));
    return toSlot(
        Complex::create(
            static_cast<double>(-value.real()),
            static_cast<double>(-value.imag()),
            code,
            mm::autoSpace()));
}

// `z ^ n` with an integer n: repeated squaring, so `z ^ 2` is exactly `z * z` and every step
// is one of the language's multiplications. A negative n is the reciprocal of the positive
// power; the zero base gives an infinity, as `0.0 ^ -1` does for the real types (no fault).
template <typename T>
std::complex<T> integerPower(const std::complex<T> &base, long long exponent) {
    const bool negative     = exponent < 0;
    unsigned long long rest = negative ? 0ULL - static_cast<unsigned long long>(exponent)
                                       : static_cast<unsigned long long>(exponent);
    std::complex<T> result{1, 0};
    std::complex<T> factor = base;
    while (rest != 0) {
        if ((rest & 1ULL) != 0) {
            result *= factor;
        }
        rest >>= 1U;
        if (rest != 0) {
            factor *= factor;
        }
    }
    if (negative) {
        result = std::complex<T>{1, 0} / result;
    }
    return result;
}

// `z ^ w`: an exponent that is a whole number (imaginary part zero, real part an integer a
// double holds exactly) is the integer power above; every other exponent goes through
// std::pow of the two parts in their width, as `^` does for the real types.
template <typename T>
std::complex<T> complexPower(const std::complex<T> &base, const std::complex<T> &exponent) {
    const T real = exponent.imag() == T{0} ? exponent.real() : std::numeric_limits<T>::quiet_NaN();
    if (std::isfinite(real) && std::trunc(real) == real &&
        real >= static_cast<T>(-9007199254740992.0) && real <= static_cast<T>(9007199254740992.0)) {
        return integerPower<T>(base, static_cast<long long>(real));
    }
    return std::pow(base, exponent);
}

template <typename T>
std::complex<T> powerOperand(const Complex *base, ArgsView &norm, size_t index) {
    if (norm.code(index) == TypeCode::Complex64 || norm.code(index) == TypeCode::Complex128) {
        return complexPower<T>(asComplex<T>(base), asComplex<T>(norm.get<Complex *>(index)));
    }
    const long long exponent = norm.code(index) == TypeCode::Int32
                                   ? static_cast<long long>(norm.get<Int32>(index))
                                   : static_cast<long long>(norm.get<Int64>(index));
    return integerPower<T>(asComplex<T>(base), exponent);
}

template <typename T> slot_t complexPowBinary(ArgsView &norm, TypeCode code, bool assign) {
    Complex *base = nullptr;
    if (assign) {
        Tuple *cell = variableOf(norm);
        base        = cell->get<Complex *>(0);
    } else {
        base = norm.get<Complex *>(0);
    }
    const std::complex<T> result = powerOperand<T>(base, norm, 1);
    const slot_t slot            = toSlot(
        Complex::create(
            static_cast<double>(result.real()),
            static_cast<double>(result.imag()),
            code,
            mm::autoSpace()));
    if (assign) {
        Tuple *cell = variableOf(norm);
        cell->set<slot_t>(0, slot, static_cast<TupleType *>(norm.type(0)));
    }
    return slot;
}

template <typename T> slot_t complexAssign(ArgsView &norm, TypeCode code, ComplexOp op) {
    Tuple *cell                  = variableOf(norm);
    const Complex *left          = cell->get<Complex *>(0);
    const Complex *right         = norm.get<Complex *>(1);
    const std::complex<T> result = complexCompute<T>(left, right, op);
    const slot_t slot            = toSlot(
        Complex::create(
            static_cast<double>(result.real()),
            static_cast<double>(result.imag()),
            code,
            mm::autoSpace()));
    cell->set<slot_t>(0, slot, static_cast<TupleType *>(norm.type(0)));
    return slot;
}

} // namespace

slot_t __builtin__var_new__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = Tuple::create(1, mm::autoSpace());
    cell->set<slot_t>(0, norm.slot(0), TupleType::cell(norm.type(0)));
    return toSlot(cell);
}

slot_t __builtin__var_get__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    // The value leaves the variable: from here on something else may refer to it, so the
    // variable no longer grows it in place (:op/assn_owned sets the mark again).
    cell->setHoldsUnsharedArray(false);
    return cell->get<slot_t>(0);
}

// A read of a variable whose value goes only to operations that cannot keep it (the length,
// an element of scalars, a comparison, ...) and that all run before the variable is written
// again (Builder::markNonRetainingReads). The array cannot be referred to by anything but the
// variable after such a read, so the read leaves the variable's mark (holdsUnsharedArray) as
// it is; otherwise it is :var/get.
slot_t __builtin__var_peek__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return variableOf(norm)->get<slot_t>(0);
}

slot_t __builtin__assn__(ArgsView &with, ArgsView &norm, Context &ctx) {
    slot_t rhs  = norm.slot(1);
    Tuple *cell = variableOf(norm);
    cell->set<slot_t>(0, rhs, static_cast<TupleType *>(norm.type(0)));
    // The value stored may be referred to from elsewhere.
    cell->setHoldsUnsharedArray(false);
    return rhs;
}

// `xs = xs->sort`, `acc = acc->append<v>` written as a statement, for an operator that
// declares an in-place form (OperatorTable::setInPlaceForm; Builder::inPlaceRebindingOf). The
// compiler writes
//
//     :op/assn_owned(xs, <the in-place form>(..., :var/take(xs)))
//
// The meaning is that of the rebinding (design principles 11.3): the operator returns a
// new value and the variable is bound to it. :var/take hands the in-place form an array no one
// else refers to: the variable's own array when the variable is its only holder (the mark set
// by :op/assn_owned), otherwise a copy of it. The form changes that array and returns it, and
// :op/assn_owned stores it and sets the mark, as nothing else refers to it. Between the two
// nothing reads the variable: the three are one statement of a sync block.
//
// Collectors: the copy is a new object, and the in-place forms reorder the slots of an array
// of numbers, which hold no references. The flag is a field of the cell (it moves with it).
slot_t __builtin__var_take__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell    = variableOf(norm);
    auto *cellType = static_cast<TupleType *>(norm.type(0));
    auto *arrType  = static_cast<ArrayType *>(cellType->typeAt(0));
    Array *arr     = cell->get<Array *>(0);
    if (cell->holdsUnsharedArray()) {
        return toSlot(arr);
    }
    return toSlot(static_cast<Array *>(arr->clone(mm::autoSpace(), arrType)));
}

slot_t __builtin__assn_owned__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const slot_t value = norm.slot(1);
    Tuple *cell        = variableOf(norm);
    cell->set<slot_t>(0, value, static_cast<TupleType *>(norm.type(0)));
    // Made by :var/take and an in-place form for this variable: no one else refers to it.
    cell->setHoldsUnsharedArray(true);
    return value;
}

slot_t __builtin__assn_add_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int32 lhs   = cell->get<Int32>(0);
    Int32 rhs   = norm.get<Int32>(1);
    lhs += rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_add_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int64 lhs   = cell->get<Int64>(0);
    Int64 rhs   = norm.get<Int64>(1);
    lhs += rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_add_f__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Float32 lhs = cell->get<Float32>(0);
    Float32 rhs = norm.get<Float32>(1);
    lhs += rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_add_d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Float64 lhs = cell->get<Float64>(0);
    Float64 rhs = norm.get<Float64>(1);
    lhs += rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_add_s__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell    = variableOf(norm);
    String *result = String::concat(cell->get<String *>(0), norm.get<String *>(1), mm::autoSpace());
    cell->set<slot_t>(0, toSlot(result), static_cast<TupleType *>(norm.type(0)));
    return toSlot(result);
}

slot_t __builtin__assn_sub_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int32 lhs   = cell->get<Int32>(0);
    Int32 rhs   = norm.get<Int32>(1);
    lhs -= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_sub_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int64 lhs   = cell->get<Int64>(0);
    Int64 rhs   = norm.get<Int64>(1);
    lhs -= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_sub_f__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Float32 lhs = cell->get<Float32>(0);
    Float32 rhs = norm.get<Float32>(1);
    lhs -= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_sub_d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Float64 lhs = cell->get<Float64>(0);
    Float64 rhs = norm.get<Float64>(1);
    lhs -= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mul_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int32 lhs   = cell->get<Int32>(0);
    Int32 rhs   = norm.get<Int32>(1);
    lhs *= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mul_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int64 lhs   = cell->get<Int64>(0);
    Int64 rhs   = norm.get<Int64>(1);
    lhs *= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mul_f__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Float32 lhs = cell->get<Float32>(0);
    Float32 rhs = norm.get<Float32>(1);
    lhs *= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mul_d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Float64 lhs = cell->get<Float64>(0);
    Float64 rhs = norm.get<Float64>(1);
    lhs *= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_div_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int32 lhs   = cell->get<Int32>(0);
    Int32 rhs   = norm.get<Int32>(1);

    if (rhs == 0) {
        throwRuntimeFault(RuntimeDiag::DivisionByZero);
    }

    lhs = camel::utils::quotient(lhs, rhs);
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_div_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int64 lhs   = cell->get<Int64>(0);
    Int64 rhs   = norm.get<Int64>(1);

    if (rhs == 0) {
        throwRuntimeFault(RuntimeDiag::DivisionByZero);
    }

    lhs = camel::utils::quotient(lhs, rhs);
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_div_f__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Float32 lhs = cell->get<Float32>(0);
    Float32 rhs = norm.get<Float32>(1);

    if (rhs == 0.0f) {
        throwRuntimeFault(RuntimeDiag::DivisionByZero);
    }

    lhs = camel::utils::quotient(lhs, rhs);
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_div_d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Float64 lhs = cell->get<Float64>(0);
    Float64 rhs = norm.get<Float64>(1);

    if (rhs == 0.0) {
        throwRuntimeFault(RuntimeDiag::DivisionByZero);
    }

    lhs = camel::utils::quotient(lhs, rhs);
    cell->set(0, lhs);
    return toSlot(lhs);
}

// `%=` on the two integer types: the remainder `%` gives, stored back.
template <typename T> static slot_t assignMod(ArgsView &norm) {
    Tuple *cell = variableOf(norm);
    T lhs       = cell->get<T>(0);
    T rhs       = norm.get<T>(1);
    if (rhs == 0) {
        throwRuntimeFault(RuntimeDiag::DivisionByZero);
    }
    lhs = camel::utils::remainder(lhs, rhs);
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mod_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return assignMod<Int32>(norm);
}

slot_t __builtin__assn_mod_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return assignMod<Int64>(norm);
}

// `^=` on the four numeric types: what `^` gives (__builtin__pow__), stored back.
template <typename T> static slot_t assignPow(ArgsView &norm) {
    Tuple *cell = variableOf(norm);
    T lhs       = static_cast<T>(std::pow(cell->get<T>(0), norm.get<T>(1)));
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_pow_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return assignPow<Int32>(norm);
}

slot_t __builtin__assn_pow_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return assignPow<Int64>(norm);
}

slot_t __builtin__assn_pow_f__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return assignPow<Float32>(norm);
}

slot_t __builtin__assn_pow_d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return assignPow<Float64>(norm);
}

// `&=` and `|=` on bool: `a = a && b` and `a = a || b` (design principles 4.2: they are the
// boolean and / or; bit operations are functions, `bit_and` and the like of `math`). The right
// side is an operand like any other, evaluated before the operator runs.
slot_t __builtin__assn_and__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Bool lhs    = cell->get<Bool>(0) && norm.get<Bool>(1);
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_or__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Bool lhs    = cell->get<Bool>(0) || norm.get<Bool>(1);
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__or__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Bool lhs = norm.get<Bool>(0);
    if (lhs) {
        // Short-circuit when the left operand is true.
        return toSlot(true);
    } else {
        Bool rhs = norm.get<Bool>(1);
        return toSlot(rhs);
    }
}

slot_t __builtin__and__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Bool lhs = norm.get<Bool>(0);
    if (!lhs) {
        // Short-circuit when the left operand is false.
        return toSlot(false);
    } else {
        Bool rhs = norm.get<Bool>(1);
        return toSlot(rhs);
    }
}

// `==` and `!=`. Floats compare as numbers (IEEE: NaN equals nothing, itself included, and
// the two zeros are equal), which is what FastVM and the JIT compile `==` on floats to;
// comparing the slots would compare bit patterns. Arrays, tuples and structs compare by
// content, element by element (design principles 4.2).
slot_t __builtin__eq__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(valuesEqualByContent(norm.slot(0), norm.slot(1), norm.type(0)));
}

slot_t __builtin__ne__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(!valuesEqualByContent(norm.slot(0), norm.slot(1), norm.type(0)));
}

// Strings are ordered by their bytes (design principles log 2.9); a proper prefix comes first.
static int compareStrings(ArgsView &norm) {
    return norm.get<String *>(0)->compare(norm.get<String *>(1));
}

slot_t __builtin__lt_s__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(Bool(compareStrings(norm) < 0));
}

slot_t __builtin__le_s__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(Bool(compareStrings(norm) <= 0));
}

slot_t __builtin__gt_s__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(Bool(compareStrings(norm) > 0));
}

slot_t __builtin__ge_s__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot(Bool(compareStrings(norm) >= 0));
}

slot_t __builtin__add_ii__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 left  = norm.get<Int32>(0);
    Int32 right = norm.get<Int32>(1);
    return toSlot(left + right);
}

slot_t __builtin__add_ll__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 left  = norm.get<Int64>(0);
    Int64 right = norm.get<Int64>(1);
    return toSlot(left + right);
}

slot_t __builtin__add_ff__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 left  = norm.get<Float32>(0);
    Float32 right = norm.get<Float32>(1);
    return toSlot(left + right);
}

slot_t __builtin__add_dd__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 left  = norm.get<Float64>(0);
    Float64 right = norm.get<Float64>(1);
    return toSlot(left + right);
}

slot_t __builtin__add_ss__(ArgsView &with, ArgsView &norm, Context &ctx) {
    String *left  = norm.get<String *>(0);
    String *right = norm.get<String *>(1);

    String *result = String::concat(left, right, mm::autoSpace());
    return toSlot(result);
}

slot_t __builtin__sub_ii__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 lhs = norm.get<Int32>(0);
    Int32 rhs = norm.get<Int32>(1);
    return toSlot(lhs - rhs);
}

slot_t __builtin__sub_ll__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 lhs = norm.get<Int64>(0);
    Int64 rhs = norm.get<Int64>(1);
    return toSlot(lhs - rhs);
}

slot_t __builtin__sub_ff__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 lhs = norm.get<Float32>(0);
    Float32 rhs = norm.get<Float32>(1);
    return toSlot(lhs - rhs);
}

slot_t __builtin__sub_dd__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 lhs = norm.get<Float64>(0);
    Float64 rhs = norm.get<Float64>(1);
    return toSlot(lhs - rhs);
}

slot_t __builtin__mul__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode lhsType = norm.code(0);

    switch (lhsType) {
    case TypeCode::Int32: {
        Int32 lhs = norm.get<Int32>(0);
        Int32 rhs = norm.get<Int32>(1);
        return toSlot(lhs * rhs);
    }

    case TypeCode::Int64: {
        Int64 lhs = norm.get<Int64>(0);
        Int64 rhs = norm.get<Int64>(1);
        return toSlot(lhs * rhs);
    }

    case TypeCode::Float32: {
        Float32 lhs = norm.get<Float32>(0);
        Float32 rhs = norm.get<Float32>(1);
        return toSlot(lhs * rhs);
    }

    case TypeCode::Float64: {
        Float64 lhs = norm.get<Float64>(0);
        Float64 rhs = norm.get<Float64>(1);
        return toSlot(lhs * rhs);
    }

    case TypeCode::Complex64:
        return complexBinary<float>(norm, TypeCode::Complex64, ComplexOp::Mul);

    case TypeCode::Complex128:
        return complexBinary<double>(norm, TypeCode::Complex128, ComplexOp::Mul);

    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "*",
            typeCodeToString(lhsType),
            "a number");
    }
}

slot_t __builtin__div__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode lhsType = norm.code(0);
    switch (lhsType) {
    case TypeCode::Int32: {
        Int32 lhs = norm.get<Int32>(0);
        Int32 rhs = norm.get<Int32>(1);
        if (rhs == 0) {
            throwRuntimeFault(RuntimeDiag::DivisionByZero);
        }
        return toSlot(camel::utils::quotient(lhs, rhs));
    }

    case TypeCode::Int64: {
        Int64 lhs = norm.get<Int64>(0);
        Int64 rhs = norm.get<Int64>(1);
        if (rhs == 0) {
            throwRuntimeFault(RuntimeDiag::DivisionByZero);
        }
        return toSlot(camel::utils::quotient(lhs, rhs));
    }

    case TypeCode::Float32: {
        Float32 lhs = norm.get<Float32>(0);
        Float32 rhs = norm.get<Float32>(1);
        if (rhs == 0.0f) {
            throwRuntimeFault(RuntimeDiag::DivisionByZero);
        }
        return toSlot(camel::utils::quotient(lhs, rhs));
    }

    case TypeCode::Float64: {
        Float64 lhs = norm.get<Float64>(0);
        Float64 rhs = norm.get<Float64>(1);
        if (rhs == 0.0) {
            throwRuntimeFault(RuntimeDiag::DivisionByZero);
        }
        return toSlot(camel::utils::quotient(lhs, rhs));
    }

    case TypeCode::Complex64:
        return complexBinary<float>(norm, TypeCode::Complex64, ComplexOp::Div);

    case TypeCode::Complex128:
        return complexBinary<double>(norm, TypeCode::Complex128, ComplexOp::Div);

    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "/",
            typeCodeToString(lhsType),
            "a number");
    }
}

slot_t __builtin__mod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode lhsType = norm.code(0);
    switch (lhsType) {
    case TypeCode::Int32: {
        Int32 lhs = norm.get<Int32>(0);
        Int32 rhs = norm.get<Int32>(1);
        if (rhs == 0) {
            throwRuntimeFault(RuntimeDiag::DivisionByZero);
        }
        return toSlot(camel::utils::remainder(lhs, rhs));
    }

    case TypeCode::Int64: {
        Int64 lhs = norm.get<Int64>(0);
        Int64 rhs = norm.get<Int64>(1);
        if (rhs == 0) {
            throwRuntimeFault(RuntimeDiag::DivisionByZero);
        }
        return toSlot(camel::utils::remainder(lhs, rhs));
    }

    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "%",
            typeCodeToString(lhsType),
            "an integer");
    }
}

// Exponentiation operator.
slot_t __builtin__pow__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode lhsType = norm.code(0);
    switch (lhsType) {
    case TypeCode::Int32: {
        Int32 base = norm.get<Int32>(0);
        Int32 exp  = norm.get<Int32>(1);
        return toSlot(static_cast<Int32>(std::pow(base, exp)));
    }

    case TypeCode::Int64: {
        Int64 base = norm.get<Int64>(0);
        Int64 exp  = norm.get<Int64>(1);
        return toSlot(static_cast<Int64>(std::pow(base, exp)));
    }

    case TypeCode::Float32: {
        Float32 base = norm.get<Float32>(0);
        Float32 exp  = norm.get<Float32>(1);
        return toSlot(static_cast<Float32>(std::pow(base, exp)));
    }

    case TypeCode::Float64: {
        Float64 base = norm.get<Float64>(0);
        Float64 exp  = norm.get<Float64>(1);
        return toSlot(std::pow(base, exp));
    }

    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "**",
            typeCodeToString(lhsType),
            "a number");
    }
}

#include <string>

slot_t __builtin__idx__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode idxType = norm.code(1);

    // Array indexing: index must be Int.
    if (idxType == TypeCode::Int64) {
        TypeCode containerType = norm.code(0);
        switch (containerType) {
        case TypeCode::Tuple: {
            Tuple *tpl = norm.get<Tuple *>(0);
            Int64 raw  = norm.get<Int64>(1);
            size_t idx = static_cast<size_t>(raw);
            if (raw < 0 || idx >= tpl->size()) {
                throwRuntimeFault(RuntimeDiag::IndexOutOfRange, raw, "a tuple", tpl->size());
            }
            return tpl->get<slot_t>(idx);
        }
        case TypeCode::Array: {
            Array *arr = norm.get<Array *>(0);
            Int64 raw  = norm.get<Int64>(1);
            size_t idx = static_cast<size_t>(raw);
            if (raw < 0 || idx >= arr->size()) {
                throwRuntimeFault(RuntimeDiag::IndexOutOfRange, raw, "an array", arr->size());
            }
            return arr->get<slot_t>(idx);
        }
        case TypeCode::String: {
            // A string is valid UTF-8 and its indices count code points (design principles
            // 11.1): `'中文'[0]` is `'中'`. The result is the string of that one character.
            String *str                 = norm.get<String *>(0);
            Int64 raw                   = norm.get<Int64>(1);
            const std::string_view text = str->view();
            // ASCII text (the common case): code point i is byte i.
            const bool ascii   = camel::utf8::isAscii(text);
            const size_t count = ascii ? text.size() : camel::utf8::count(text);
            if (raw < 0 || static_cast<size_t>(raw) >= count) {
                throwRuntimeFault(RuntimeDiag::IndexOutOfRange, raw, "a string", count);
            }
            const size_t from = ascii ? static_cast<size_t>(raw)
                                      : camel::utf8::offsetOf(text, static_cast<size_t>(raw));
            const size_t to =
                from + camel::utf8::sequenceLength(static_cast<unsigned char>(text[from]));
            return toSlot(String::from(std::string(text.substr(from, to - from)), mm::autoSpace()));
        }
        case TypeCode::Struct: {
            Struct *st = norm.get<Struct *>(0);
            Int64 raw  = norm.get<Int64>(1);
            size_t idx = static_cast<size_t>(raw);
            if (raw < 0 || idx >= st->size()) {
                throwRuntimeFault(RuntimeDiag::IndexOutOfRange, raw, "a struct", st->size());
            }
            return st->get<slot_t>(idx);
        }
        default: {
            throwRuntimeFault(
                RuntimeDiag::OperandTypeUnsupported,
                "[]",
                typeCodeToString(containerType),
                "an array, a tuple, a struct or a string");
        }
        }
    }

    // Struct indexing: index must be String.
    if (idxType == TypeCode::String) {
        Struct *st                  = norm.get<Struct *>(0);
        String *keyObj              = norm.get<String *>(1);
        const std::string_view &key = keyObj->view();
        const Type *structType      = norm.type(0);
        if (!st->has(key, structType)) {
            throwRuntimeFault(RuntimeDiag::FieldNotFound, std::string(key));
        }
        return st->get<slot_t>(key, structType);
    }

    throwRuntimeFault(
        RuntimeDiag::OperandTypeUnsupported,
        "[]",
        typeCodeToString(idxType),
        "an integer index, or a field name for a struct");
}

slot_t __builtin__not__(ArgsView &with, ArgsView &norm, Context &) {
    Bool v = norm.get<Bool>(0);
    return toSlot(!v);
}

slot_t __builtin__neg__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode t = norm.code(0);
    switch (t) {
    case TypeCode::Int32: {
        Int32 v = norm.get<Int32>(0);
        return toSlot(-v);
    }

    case TypeCode::Int64: {
        Int64 v = norm.get<Int64>(0);
        return toSlot(-v);
    }

    case TypeCode::Float32: {
        Float32 v = norm.get<Float32>(0);
        return toSlot(-v);
    }

    case TypeCode::Float64: {
        Float64 v = norm.get<Float64>(0);
        return toSlot(-v);
    }

    case TypeCode::Complex64:
        return complexNegate<float>(norm, TypeCode::Complex64);

    case TypeCode::Complex128:
        return complexNegate<double>(norm, TypeCode::Complex128);

    // The two reduced-precision floating types (design principles 7.3): negation is exact for
    // them, so it flips the sign bit of the value itself, computed in f32 and rounded once.
    case TypeCode::BFloat16:
        return __builtin__neg_bf__(with, norm, ctx);

    case TypeCode::Float16:
        return __builtin__neg_f6__(with, norm, ctx);

    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "-",
            typeCodeToString(t),
            "a number");
    }
}

slot_t __builtin__inv__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode t = norm.code(0);
    switch (t) {
    case TypeCode::Int32: {
        Int32 v = norm.get<Int32>(0);
        return toSlot(~v);
    }

    case TypeCode::Int64: {
        Int64 v = norm.get<Int64>(0);
        return toSlot(~v);
    }

    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "~",
            typeCodeToString(t),
            "an integer");
    }
}

// ---- The complex operators (suffix `c` is c64, `z` is c128) --------------------------------

slot_t __builtin__add_cc__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexBinary<float>(norm, TypeCode::Complex64, ComplexOp::Add);
}
slot_t __builtin__add_zz__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexBinary<double>(norm, TypeCode::Complex128, ComplexOp::Add);
}
slot_t __builtin__sub_cc__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexBinary<float>(norm, TypeCode::Complex64, ComplexOp::Sub);
}
slot_t __builtin__sub_zz__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexBinary<double>(norm, TypeCode::Complex128, ComplexOp::Sub);
}
slot_t __builtin__mul_cc__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexBinary<float>(norm, TypeCode::Complex64, ComplexOp::Mul);
}
slot_t __builtin__mul_zz__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexBinary<double>(norm, TypeCode::Complex128, ComplexOp::Mul);
}
slot_t __builtin__div_cc__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexBinary<float>(norm, TypeCode::Complex64, ComplexOp::Div);
}
slot_t __builtin__div_zz__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexBinary<double>(norm, TypeCode::Complex128, ComplexOp::Div);
}
slot_t __builtin__neg_c__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexNegate<float>(norm, TypeCode::Complex64);
}
slot_t __builtin__neg_z__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexNegate<double>(norm, TypeCode::Complex128);
}

slot_t __builtin__assn_add_c__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexAssign<float>(norm, TypeCode::Complex64, ComplexOp::Add);
}
slot_t __builtin__assn_add_z__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexAssign<double>(norm, TypeCode::Complex128, ComplexOp::Add);
}
slot_t __builtin__assn_sub_c__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexAssign<float>(norm, TypeCode::Complex64, ComplexOp::Sub);
}
slot_t __builtin__assn_sub_z__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexAssign<double>(norm, TypeCode::Complex128, ComplexOp::Sub);
}
slot_t __builtin__assn_mul_c__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexAssign<float>(norm, TypeCode::Complex64, ComplexOp::Mul);
}
slot_t __builtin__assn_mul_z__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexAssign<double>(norm, TypeCode::Complex128, ComplexOp::Mul);
}
slot_t __builtin__assn_div_c__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexAssign<float>(norm, TypeCode::Complex64, ComplexOp::Div);
}
slot_t __builtin__assn_div_z__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexAssign<double>(norm, TypeCode::Complex128, ComplexOp::Div);
}
slot_t __builtin__pow_c__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexPowBinary<float>(norm, TypeCode::Complex64, false);
}
slot_t __builtin__pow_z__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexPowBinary<double>(norm, TypeCode::Complex128, false);
}
slot_t __builtin__assn_pow_c__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexPowBinary<float>(norm, TypeCode::Complex64, true);
}
slot_t __builtin__assn_pow_z__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return complexPowBinary<double>(norm, TypeCode::Complex128, true);
}
