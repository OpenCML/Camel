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
 * Created: Jul. 29, 2025
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "ops.h"
#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/array.h"
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

#include <cmath>

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
} // namespace

slot_t __builtin__var_new__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = Tuple::create(1, mm::autoSpace());
    cell->set<slot_t>(0, norm.slot(0), TupleType::cell(norm.type(0)));
    return toSlot(cell);
}

slot_t __builtin__var_get__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    // The value leaves the variable: from here on something else may refer to it, so the
    // variable no longer grows it in place (__builtin__assn_append__).
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

// `acc = acc->append<v>` written as a statement (Builder::rebindingAppendOf). The meaning is
// that of the rebinding: `append` returns a new array and the variable is bound to it, the
// old array unchanged (design principles 2.12). Changing the array in place gives the same
// result when nothing but the variable refers to it, which the cell records
// (Tuple::holdsUnsharedArray): the flag is set only here, after storing an array made here,
// and cleared by every read of the variable (:var/get) and every other write (:op/assn), so
// while it is set no reference to the array has left the cell. The first append after the
// variable was read copies (with room to grow); the appends that follow, as long as nothing
// reads the variable in between, grow that copy in place, as `xs->append<v>` used to.
//
// Collectors: the array is changed with the write barrier of Array::append, as `append` used
// to change it; the cell keeps it alive and a moving collector updates the cell's slot. The
// flag is a field of the cell, so it moves with it. Parallel scheduler: the operator runs on one
// thread and reads and writes only its own cell; two threads use one cell at the same time
// only when the elements of a parallel `map` write a captured `var`, whose order the
// language leaves to the program (design principles 2.7) and which raced already when
// `append` changed the array in place.
slot_t __builtin__assn_append__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell        = variableOf(norm);
    auto *cellType     = static_cast<TupleType *>(norm.type(0));
    auto *arrType      = static_cast<ArrayType *>(cellType->typeAt(0));
    const slot_t value = norm.slot(1);
    if (cell->holdsUnsharedArray()) {
        Array *arr = cell->get<Array *>(0);
        arr->append(value, arrType);
        return toSlot(arr);
    }
    const Array *old = cell->get<Array *>(0);
    const size_t n   = old->size();
    // Room for the appends that are likely to follow, so that a loop of them stays linear.
    // (Allocation never collects or moves an object: `old` stays valid; mm/gen.h.)
    Array *res = Array::create(mm::autoSpace(), 0);
    res->reserve(n + n / 2 + 1, arrType);
    for (size_t i = 0; i < n; ++i) {
        res->append(old->get<slot_t>(i), arrType);
    }
    res->append(value, arrType);
    cell->set<slot_t>(0, toSlot(res), cellType);
    cell->setHoldsUnsharedArray(true);
    return toSlot(res);
}

// `xs = xs->sort` written as a statement, for an operator that declares an in-place form
// (OperatorTable::setInPlaceForm; Builder::inPlaceRebindingOf). The compiler writes
//
//     :op/assn_owned(xs, <the in-place form>(..., :var/take(xs)))
//
// The meaning is that of the rebinding (design principles 2.12, 2.13): the operator returns a
// new array and the variable is bound to it. :var/take hands the in-place form an array no one
// else refers to: the variable's own array when the variable is its only holder (the mark
// __builtin__assn_append__ describes), otherwise a copy of it. The form changes that array and
// returns it, and :op/assn_owned stores it and sets the mark, as nothing else refers to it.
// Between the two nothing reads the variable: the three are one statement of a sync block.
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

// `&=` and `|=` on bool: `a = a && b` and `a = a || b` (design principles 2.10: they are the
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
// content, element by element (design principles 2.10).
slot_t __builtin__eq__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(valuesEqualByContent(norm.slot(0), norm.slot(1), norm.type(0)));
}

slot_t __builtin__ne__(ArgsView &with, ArgsView &norm, Context &ctx) {
    return toSlot<Bool>(!valuesEqualByContent(norm.slot(0), norm.slot(1), norm.type(0)));
}

// Strings are ordered by their bytes (design principles 2.9); a proper prefix comes first.
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
            // 2.17): `'中文'[0]` is `'中'`. The result is the string of that one character.
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
