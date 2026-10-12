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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "ops.h"
#include "camel/core/context/context.h"
#include "camel/core/mm.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/assert.h"
#include "camel/utils/int_div.h"

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
    return variableOf(norm)->get<slot_t>(0);
}

slot_t __builtin__assn__(ArgsView &with, ArgsView &norm, Context &ctx) {
    slot_t rhs = norm.slot(1);
    variableOf(norm)->set<slot_t>(0, rhs, static_cast<TupleType *>(norm.type(0)));
    return rhs;
}

slot_t __builtin__assn_add_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int32 lhs = cell->get<Int32>(0);
    Int32 rhs = norm.get<Int32>(1);
    lhs += rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_add_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int64 lhs = cell->get<Int64>(0);
    Int64 rhs = norm.get<Int64>(1);
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
    Int32 lhs = cell->get<Int32>(0);
    Int32 rhs = norm.get<Int32>(1);
    lhs -= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_sub_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int64 lhs = cell->get<Int64>(0);
    Int64 rhs = norm.get<Int64>(1);
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
    Int32 lhs = cell->get<Int32>(0);
    Int32 rhs = norm.get<Int32>(1);
    lhs *= rhs;
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mul_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int64 lhs = cell->get<Int64>(0);
    Int64 rhs = norm.get<Int64>(1);
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
    Int32 lhs = cell->get<Int32>(0);
    Int32 rhs = norm.get<Int32>(1);

    if (rhs == 0) {
        throwRuntimeFault(RuntimeDiag::DivisionByZero);
    }

    lhs = camel::utils::quotient(lhs, rhs);
    cell->set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_div_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Tuple *cell = variableOf(norm);
    Int64 lhs = cell->get<Int64>(0);
    Int64 rhs = norm.get<Int64>(1);

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

// `&=` and `|=` on bool: `a = a && b` and `a = a || b`. The right side is an operand like any
// other, evaluated before the operator runs.
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

slot_t __builtin__eq__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode lhsType = norm.code(0);

    if (isGCTraced(lhsType)) {
        Object *lhs    = norm.get<Object *>(0);
        Object *rhs    = norm.get<Object *>(1);
        const Type *ty = norm.type(0);
        Bool res       = lhs->equals(rhs, ty, false);
        return toSlot(res);
    } else {
        slot_t lhs = norm.slot(0);
        slot_t rhs = norm.slot(1);
        Bool res   = lhs == rhs;
        return toSlot(res);
    }
}

slot_t __builtin__ne__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode lhsType = norm.code(0);

    if (isGCTraced(lhsType)) {
        Object *lhs    = norm.get<Object *>(0);
        Object *rhs    = norm.get<Object *>(1);
        const Type *ty = norm.type(0);
        Bool res       = !lhs->equals(rhs, ty, false);
        return toSlot(res);
    } else {
        slot_t lhs = norm.slot(0);
        slot_t rhs = norm.slot(1);
        Bool res   = !(lhs == rhs);
        return toSlot(res);
    }
}

slot_t __builtin__strict_eq__(ArgsView &with, ArgsView &norm, Context &ctx) {
    slot_t lhs = norm.slot(0);
    slot_t rhs = norm.slot(1);
    Bool res   = (lhs == rhs);
    return toSlot(res);
}

slot_t __builtin__strict_ne__(ArgsView &with, ArgsView &norm, Context &ctx) {
    slot_t lhs = norm.slot(0);
    slot_t rhs = norm.slot(1);
    Bool res   = (lhs != rhs);
    return toSlot(res);
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
            throwRuntimeFault(
                RuntimeDiag::FieldNotFound,
                std::string(key));
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
