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
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "ops.h"
#include "builtin/datas/tensor.h"
#include "builtin/types/tensor.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/operator.h"
#include "utils/assert.h"

#include <cmath>

namespace GIR = GraphIR;

slot_t __builtin__assn__(ArgsView &with, ArgsView &norm, Context &ctx) {
    // const data_ptr_t &rhs = frame.get(nargs[1]);

    // if (rhs->type()->code() == TensorType::typeCode()) {
    //     auto rhs_tensor = std::dynamic_pointer_cast<TensorData>(rhs);
    //     if (rhs_tensor) {
    //         const data_ptr_t &lhs = frame.get(nargs[0]);
    //         auto lhs_tensor       = std::dynamic_pointer_cast<TensorData>(lhs);

    //         if (lhs_tensor) {
    //             lhs_tensor->assign(rhs_tensor);
    //             frame.set(self, lhs_tensor);
    //             return;
    //         } else {
    //             auto new_tensor = std::dynamic_pointer_cast<TensorData>(rhs_tensor->clone(true));
    //             frame.set(nargs[0], new_tensor);
    //             frame.set(self, new_tensor);
    //             return;
    //         }
    //     }
    // }

    // frame.set(nargs[0], rhs);
    // frame.set(self, rhs);
    ASSERT(false, "__builtin__assn__ not implemented yet");
    return NullSlot;
}

slot_t __builtin__assn_add_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 lhs = norm.get<Int32>(0);
    Int32 rhs = norm.get<Int32>(1);
    lhs += rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_add_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 lhs = norm.get<Int64>(0);
    Int64 rhs = norm.get<Int64>(1);
    lhs += rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_add_f__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 lhs = norm.get<Float32>(0);
    Float32 rhs = norm.get<Float32>(1);
    lhs += rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_add_d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 lhs = norm.get<Float64>(0);
    Float64 rhs = norm.get<Float64>(1);
    lhs += rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_sub_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 lhs = norm.get<Int32>(0);
    Int32 rhs = norm.get<Int32>(1);
    lhs -= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_sub_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 lhs = norm.get<Int64>(0);
    Int64 rhs = norm.get<Int64>(1);
    lhs -= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_sub_f__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 lhs = norm.get<Float32>(0);
    Float32 rhs = norm.get<Float32>(1);
    lhs -= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_sub_d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 lhs = norm.get<Float64>(0);
    Float64 rhs = norm.get<Float64>(1);
    lhs -= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mul_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 lhs = norm.get<Int32>(0);
    Int32 rhs = norm.get<Int32>(1);
    lhs *= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mul_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 lhs = norm.get<Int64>(0);
    Int64 rhs = norm.get<Int64>(1);
    lhs *= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mul_f__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 lhs = norm.get<Float32>(0);
    Float32 rhs = norm.get<Float32>(1);
    lhs *= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mul_d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 lhs = norm.get<Float64>(0);
    Float64 rhs = norm.get<Float64>(1);
    lhs *= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_div_i__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int32 lhs = norm.get<Int32>(0);
    Int32 rhs = norm.get<Int32>(1);

    if (rhs == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
        return NullSlot;
    }

    lhs /= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_div_l__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Int64 lhs = norm.get<Int64>(0);
    Int64 rhs = norm.get<Int64>(1);

    if (rhs == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
        return NullSlot;
    }

    lhs /= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_div_f__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float32 lhs = norm.get<Float32>(0);
    Float32 rhs = norm.get<Float32>(1);

    if (rhs == 0.0f) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
        return NullSlot;
    }

    lhs /= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_div_d__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Float64 lhs = norm.get<Float64>(0);
    Float64 rhs = norm.get<Float64>(1);

    if (rhs == 0.0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
        return NullSlot;
    }

    lhs /= rhs;
    norm.set(0, lhs);
    return toSlot(lhs);
}

slot_t __builtin__assn_mod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
    return NullSlot;
}

slot_t __builtin__assn_pow__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
    return NullSlot;
}

slot_t __builtin__assn_and__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
    return NullSlot;
}

slot_t __builtin__assn_or__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
    return NullSlot;
}

slot_t __builtin__assn_mat__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Invalid tensor assignment operation");
    return NullSlot;
}

slot_t __builtin__or__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Bool lhs = norm.get<Bool>(0);
    if (lhs) {
        // 左值为真则短路
        return toSlot(true);
    } else {
        Bool rhs = norm.get<Bool>(1);
        return toSlot(rhs);
    }
}

slot_t __builtin__and__(ArgsView &with, ArgsView &norm, Context &ctx) {
    Bool lhs = norm.get<Bool>(0);
    if (!lhs) {
        // 左值为假则短路
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
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mul> operator not supported for type " + typeCodeToString(lhsType));
        return NullSlot;
    }
}

slot_t __builtin__div__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode lhsType = norm.code(0);
    switch (lhsType) {
    case TypeCode::Int32: {
        Int32 lhs = norm.get<Int32>(0);
        Int32 rhs = norm.get<Int32>(1);
        if (rhs == 0) {
            ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
            return NullSlot;
        }
        return toSlot(lhs / rhs);
    }

    case TypeCode::Int64: {
        Int64 lhs = norm.get<Int64>(0);
        Int64 rhs = norm.get<Int64>(1);
        if (rhs == 0) {
            ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
            return NullSlot;
        }
        return toSlot(lhs / rhs);
    }

    case TypeCode::Float32: {
        Float32 lhs = norm.get<Float32>(0);
        Float32 rhs = norm.get<Float32>(1);
        if (rhs == 0.0f) {
            ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
            return NullSlot;
        }
        return toSlot(lhs / rhs);
    }

    case TypeCode::Float64: {
        Float64 lhs = norm.get<Float64>(0);
        Float64 rhs = norm.get<Float64>(1);
        if (rhs == 0.0) {
            ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
            return NullSlot;
        }
        return toSlot(lhs / rhs);
    }

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<div> operator not supported for type " + typeCodeToString(lhsType));
        return NullSlot;
    }
}

slot_t __builtin__mod__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode lhsType = norm.code(0);
    switch (lhsType) {
    case TypeCode::Int32: {
        Int32 lhs = norm.get<Int32>(0);
        Int32 rhs = norm.get<Int32>(1);
        if (rhs == 0) {
            ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<mod> division by zero");
            return NullSlot;
        }
        return toSlot(lhs % rhs);
    }

    case TypeCode::Int64: {
        Int64 lhs = norm.get<Int64>(0);
        Int64 rhs = norm.get<Int64>(1);
        if (rhs == 0) {
            ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<mod> division by zero");
            return NullSlot;
        }
        return toSlot(lhs % rhs);
    }

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mod> operator only supports integer types");
        return NullSlot;
    }
}

slot_t __builtin__mat__(ArgsView &with, ArgsView &norm, Context &ctx) {
    ASSERT(false, "mat operator not implemented");
    return NullSlot;
}

// 幂运算
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
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<pow> operator not supported for type " + typeCodeToString(lhsType));
        return NullSlot;
    }
}

#include <string>

slot_t __builtin__idx__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode idxType = norm.code(1);

    // 数组索引：index 为 Int
    if (idxType == TypeCode::Int64) {
        TypeCode containerType = norm.code(0);
        switch (containerType) {
        case TypeCode::Tuple: {
            Tuple *tpl = norm.get<Tuple *>(0);
            size_t idx = static_cast<size_t>(norm.get<Int64>(1));
            if (idx >= tpl->size()) {
                ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Tuple index out of bounds.");
                return NullSlot;
            }
            return tpl->get<slot_t>(idx);
        }
        case TypeCode::Array: {
            Array *arr = norm.get<Array *>(0);
            size_t idx = static_cast<size_t>(norm.get<Int64>(1));
            if (idx >= arr->size()) {
                ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Array index out of bounds.");
                return NullSlot;
            }
            return arr->get<slot_t>(idx);
        }
        case TypeCode::Struct: {
            Struct *st = norm.get<Struct *>(0);
            size_t idx = static_cast<size_t>(norm.get<Int64>(1));
            return st->get<slot_t>(idx);
        }
        default: {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("<idx> operator not supported for type " + typeCodeToString(containerType));
            return NullSlot;
        }
        }
    }

    // 结构体索引：index 为 String
    if (idxType == TypeCode::String) {
        Struct *st                  = norm.get<Struct *>(0);
        String *keyObj              = norm.get<String *>(1);
        const std::string_view &key = keyObj->view();
        const Type *structType      = norm.type(0);
        if (!st->has(key, structType)) {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("Struct does not have field: " + std::string(key));
            return NullSlot;
        }
        return st->get<slot_t>(key, structType);
    }

    // 不支持的索引类型
    return NullSlot;
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
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<neg> operator not supported for type " + typeCodeToString(t));
        return NullSlot;
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
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(
                "<inv> operator only supported for integer types (Int / Long), got " +
                typeCodeToString(t));
        return NullSlot;
    }
}
