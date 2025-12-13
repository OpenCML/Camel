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
 * Updated: Dec. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "ops.h"
#include "builtin/datas/tensor.h"
#include "builtin/types/tensor.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "utils/assert.h"

#include <cmath>

namespace GIR = GraphIR;

void __builtin__assn__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
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
}

void __builtin__assn_add_i__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int lhs = frame.get<Int>(nargs[0]);
    Int rhs = frame.get<Int>(nargs[1]);
    lhs += rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_add_l__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Long lhs = frame.get<Long>(nargs[0]);
    Long rhs = frame.get<Long>(nargs[1]);
    lhs += rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_add_f__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float lhs = frame.get<Float>(nargs[0]);
    Float rhs = frame.get<Float>(nargs[1]);
    lhs += rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_add_d__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Double lhs = frame.get<Double>(nargs[0]);
    Double rhs = frame.get<Double>(nargs[1]);
    lhs += rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_sub_i__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int lhs = frame.get<Int>(nargs[0]);
    Int rhs = frame.get<Int>(nargs[1]);
    lhs -= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_sub_l__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Long lhs = frame.get<Long>(nargs[0]);
    Long rhs = frame.get<Long>(nargs[1]);
    lhs -= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_sub_f__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float lhs = frame.get<Float>(nargs[0]);
    Float rhs = frame.get<Float>(nargs[1]);
    lhs -= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_sub_d__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Double lhs = frame.get<Double>(nargs[0]);
    Double rhs = frame.get<Double>(nargs[1]);
    lhs -= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_mul_i__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int lhs = frame.get<Int>(nargs[0]);
    Int rhs = frame.get<Int>(nargs[1]);
    lhs *= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_mul_l__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Long lhs = frame.get<Long>(nargs[0]);
    Long rhs = frame.get<Long>(nargs[1]);
    lhs *= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_mul_f__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float lhs = frame.get<Float>(nargs[0]);
    Float rhs = frame.get<Float>(nargs[1]);
    lhs *= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_mul_d__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Double lhs = frame.get<Double>(nargs[0]);
    Double rhs = frame.get<Double>(nargs[1]);
    lhs *= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_div_i__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int lhs = frame.get<Int>(nargs[0]);
    Int rhs = frame.get<Int>(nargs[1]);

    if (rhs == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
        frame.set(self, NullSlot);
        return;
    }

    lhs /= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_div_l__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Long lhs = frame.get<Long>(nargs[0]);
    Long rhs = frame.get<Long>(nargs[1]);

    if (rhs == 0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
        frame.set(self, NullSlot);
        return;
    }

    lhs /= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_div_f__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float lhs = frame.get<Float>(nargs[0]);
    Float rhs = frame.get<Float>(nargs[1]);

    if (rhs == 0.0f) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
        frame.set(self, NullSlot);
        return;
    }

    lhs /= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_div_d__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Double lhs = frame.get<Double>(nargs[0]);
    Double rhs = frame.get<Double>(nargs[1]);

    if (rhs == 0.0) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
        frame.set(self, NullSlot);
        return;
    }

    lhs /= rhs;
    frame.set(nargs[0], lhs);
    frame.set(self, lhs);
}

void __builtin__assn_mod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
}

void __builtin__assn_pow__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
}

void __builtin__assn_and__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
}

void __builtin__assn_or__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
}

void __builtin__assn_mat__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Invalid tensor assignment operation");
}

void __builtin__or__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Bool lhs = frame.get<Bool>(nargs[0]);
    if (lhs) {
        // 左值为真则短路
        frame.set(self, true);
    } else {
        Bool rhs = frame.get<Bool>(nargs[1]);
        frame.set(self, rhs);
    }
}

void __builtin__and__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Bool lhs = frame.get<Bool>(nargs[0]);
    if (!lhs) {
        // 左值为假则短路
        frame.set(self, false);
    } else {
        Bool rhs = frame.get<Bool>(nargs[1]);
        frame.set(self, rhs);
    }
}

void __builtin__eq__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    TypeCode lhsType = frame.typeAt(nargs[0]);

    if (isGCTraced(lhsType)) {
        Object *lhs = frame.get<Object *>(nargs[0]);
        Object *rhs = frame.get<Object *>(nargs[1]);
        Bool res    = lhs->equals(rhs, false);
        frame.set(self, res);
    } else {
        slot_t lhs = frame.get<slot_t>(nargs[0]);
        slot_t rhs = frame.get<slot_t>(nargs[1]);
        Bool res   = lhs == rhs;
        frame.set(self, res);
    }
}

void __builtin__ne__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    TypeCode lhsType = frame.typeAt(nargs[0]);

    if (isGCTraced(lhsType)) {
        Object *lhs = frame.get<Object *>(nargs[0]);
        Object *rhs = frame.get<Object *>(nargs[1]);
        Bool res    = !lhs->equals(rhs, false);
        frame.set(self, res);
    } else {
        slot_t lhs = frame.get<slot_t>(nargs[0]);
        slot_t rhs = frame.get<slot_t>(nargs[1]);
        Bool res   = !(lhs == rhs);
        frame.set(self, res);
    }
}

void __builtin__strict_eq__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    slot_t lhs = frame.get<slot_t>(nargs[0]);
    slot_t rhs = frame.get<slot_t>(nargs[1]);
    Bool res   = (lhs == rhs);
    frame.set(self, res);
}

void __builtin__strict_ne__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    slot_t lhs = frame.get<slot_t>(nargs[0]);
    slot_t rhs = frame.get<slot_t>(nargs[1]);
    Bool res   = (lhs != rhs);
    frame.set(self, res);
}

void __builtin__add_ii__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int left  = frame.get<Int>(nargs[0]);
    Int right = frame.get<Int>(nargs[1]);
    frame.set(self, left + right);
}

void __builtin__add_ll__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Long left  = frame.get<Long>(nargs[0]);
    Long right = frame.get<Long>(nargs[1]);
    frame.set(self, left + right);
}

void __builtin__add_ff__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float left  = frame.get<Float>(nargs[0]);
    Float right = frame.get<Float>(nargs[1]);
    frame.set(self, left + right);
}

void __builtin__add_dd__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Double left  = frame.get<Double>(nargs[0]);
    Double right = frame.get<Double>(nargs[1]);
    frame.set(self, left + right);
}

void __builtin__add_ss__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    String *left  = frame.get<String *>(nargs[0]);
    String *right = frame.get<String *>(nargs[1]);

    String *result = String::concat(left, right, mm::autoSpace());

    frame.set(self, result);
}

void __builtin__sub_ii__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Int lhs = frame.get<Int>(nargs[0]);
    Int rhs = frame.get<Int>(nargs[1]);
    frame.set(self, lhs - rhs);
}

void __builtin__sub_ll__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Long lhs = frame.get<Long>(nargs[0]);
    Long rhs = frame.get<Long>(nargs[1]);
    frame.set(self, lhs - rhs);
}

void __builtin__sub_ff__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Float lhs = frame.get<Float>(nargs[0]);
    Float rhs = frame.get<Float>(nargs[1]);
    frame.set(self, lhs - rhs);
}

void __builtin__sub_dd__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    Double lhs = frame.get<Double>(nargs[0]);
    Double rhs = frame.get<Double>(nargs[1]);
    frame.set(self, lhs - rhs);
}

void __builtin__mul__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    TypeCode lhsType = frame.typeAt(nargs[0]);

    switch (lhsType) {
    case TypeCode::Int: {
        Int lhs = frame.get<Int>(nargs[0]);
        Int rhs = frame.get<Int>(nargs[1]);
        frame.set(self, lhs * rhs);
        break;
    }

    case TypeCode::Long: {
        Long lhs = frame.get<Long>(nargs[0]);
        Long rhs = frame.get<Long>(nargs[1]);
        frame.set(self, lhs * rhs);
        break;
    }

    case TypeCode::Float: {
        Float lhs = frame.get<Float>(nargs[0]);
        Float rhs = frame.get<Float>(nargs[1]);
        frame.set(self, lhs * rhs);
        break;
    }

    case TypeCode::Double: {
        Double lhs = frame.get<Double>(nargs[0]);
        Double rhs = frame.get<Double>(nargs[1]);
        frame.set(self, lhs * rhs);
        break;
    }

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mul> operator not supported for type " + typeCodeToString(lhsType));
        frame.set(self, NullSlot);
        break;
    }
}

void __builtin__div__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode lhsType = frame.typeAt(nargs[0]);
    switch (lhsType) {
    case TypeCode::Int: {
        Int lhs = frame.get<Int>(nargs[0]);
        Int rhs = frame.get<Int>(nargs[1]);
        if (rhs == 0) {
            ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
            frame.set(self, NullSlot);
            return;
        }
        frame.set(self, lhs / rhs);
        break;
    }

    case TypeCode::Long: {
        Long lhs = frame.get<Long>(nargs[0]);
        Long rhs = frame.get<Long>(nargs[1]);
        if (rhs == 0) {
            ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
            frame.set(self, NullSlot);
            return;
        }
        frame.set(self, lhs / rhs);
        break;
    }

    case TypeCode::Float: {
        Float lhs = frame.get<Float>(nargs[0]);
        Float rhs = frame.get<Float>(nargs[1]);
        if (rhs == 0.0f) {
            ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
            frame.set(self, NullSlot);
            return;
        }
        frame.set(self, lhs / rhs);
        break;
    }

    case TypeCode::Double: {
        Double lhs = frame.get<Double>(nargs[0]);
        Double rhs = frame.get<Double>(nargs[1]);
        if (rhs == 0.0) {
            ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
            frame.set(self, NullSlot);
            return;
        }
        frame.set(self, lhs / rhs);
        break;
    }

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<div> operator not supported for type " + typeCodeToString(lhsType));
        frame.set(self, NullSlot);
        break;
    }
}

void __builtin__mod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode lhsType = frame.typeAt(nargs[0]);
    switch (lhsType) {
    case TypeCode::Int: {
        Int lhs = frame.get<Int>(nargs[0]);
        Int rhs = frame.get<Int>(nargs[1]);
        if (rhs == 0) {
            ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<mod> division by zero");
            frame.set(self, NullSlot);
            return;
        }
        frame.set(self, lhs % rhs);
        break;
    }

    case TypeCode::Long: {
        Long lhs = frame.get<Long>(nargs[0]);
        Long rhs = frame.get<Long>(nargs[1]);
        if (rhs == 0) {
            ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<mod> division by zero");
            frame.set(self, NullSlot);
            return;
        }
        frame.set(self, lhs % rhs);
        break;
    }

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mod> operator only supports integer types");
        frame.set(self, NullSlot);
        break;
    }
}

void __builtin__mat__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    ASSERT(false, "mat operator not implemented");
}

// 幂运算
void __builtin__pow__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode lhsType = frame.typeAt(nargs[0]);
    switch (lhsType) {
    case TypeCode::Int: {
        Int base = frame.get<Int>(nargs[0]);
        Int exp  = frame.get<Int>(nargs[1]);
        frame.set(self, static_cast<Int>(std::pow(base, exp)));
        break;
    }

    case TypeCode::Long: {
        Long base = frame.get<Long>(nargs[0]);
        Long exp  = frame.get<Long>(nargs[1]);
        frame.set(self, static_cast<Long>(std::pow(base, exp)));
        break;
    }

    case TypeCode::Float: {
        Float base = frame.get<Float>(nargs[0]);
        Float exp  = frame.get<Float>(nargs[1]);
        frame.set(self, static_cast<Float>(std::pow(base, exp)));
        break;
    }

    case TypeCode::Double: {
        Double base = frame.get<Double>(nargs[0]);
        Double exp  = frame.get<Double>(nargs[1]);
        frame.set(self, std::pow(base, exp));
        break;
    }

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<pow> operator not supported for type " + typeCodeToString(lhsType));
        frame.set(self, NullSlot);
        break;
    }
}

#include <string>

void __builtin__idx__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode idxType = frame.typeAt(nargs[1]);

    // 数组索引：index 为 Int
    if (idxType == TypeCode::Int) {
        TypeCode containerType = frame.typeAt(nargs[0]);
        switch (containerType) {
        case TypeCode::Tuple: {
            Tuple *tpl = frame.get<Tuple *>(nargs[0]);
            size_t idx = static_cast<size_t>(frame.get<Int>(nargs[1]));
            if (idx >= tpl->size()) {
                ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Tuple index out of bounds.");
                frame.set(self, NullSlot);
                return;
            }
            frame.set(self, tpl->get<slot_t>(idx));
            return;
        }
        case TypeCode::Array: {
            Array *arr = frame.get<Array *>(nargs[0]);
            size_t idx = static_cast<size_t>(frame.get<Int>(nargs[1]));
            if (idx >= arr->size()) {
                ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Array index out of bounds.");
                frame.set(self, NullSlot);
                return;
            }
            frame.set(self, arr->get<slot_t>(idx));
            return;
        }
        case TypeCode::Struct: {
            Struct *st = frame.get<Struct *>(nargs[0]);
            size_t idx = static_cast<size_t>(frame.get<Int>(nargs[1]));
            slot_t val = st->get<slot_t>(idx);
            frame.set(self, val);
            return;
        }
        default: {
            ctx.rtmDiags()
                ->of(RuntimeDiag::RuntimeError)
                .commit("<idx> operator not supported for type " + typeCodeToString(containerType));
            frame.set(self, NullSlot);
            return;
        }
        }

        // 结构体索引：index 为 String
        if (idxType == TypeCode::String) {
            Struct *st                  = frame.get<Struct *>(nargs[0]);
            String *keyObj              = frame.get<String *>(nargs[1]);
            const std::string_view &key = keyObj->view();
            if (!st->has(key)) {
                ctx.rtmDiags()
                    ->of(RuntimeDiag::RuntimeError)
                    .commit("Struct does not have field: " + std::string(key));
                frame.set(self, NullSlot);
                return;
            }
            slot_t val = st->get<slot_t>(key);
            frame.set(self, val);
            return;
        }

        // 不支持的索引类型
        frame.set(self, NullSlot);
    }
}

void __builtin__not__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &) {
    Bool v = frame.get<Bool>(nargs[0]);
    frame.set(self, !v);
}

void __builtin__neg__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode t = frame.typeAt(nargs[0]);
    switch (t) {
    case TypeCode::Int: {
        Int v = frame.get<Int>(nargs[0]);
        frame.set(self, -v);
        break;
    }

    case TypeCode::Long: {
        Long v = frame.get<Long>(nargs[0]);
        frame.set(self, -v);
        break;
    }

    case TypeCode::Float: {
        Float v = frame.get<Float>(nargs[0]);
        frame.set(self, -v);
        break;
    }

    case TypeCode::Double: {
        Double v = frame.get<Double>(nargs[0]);
        frame.set(self, -v);
        break;
    }

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<neg> operator not supported for type " + typeCodeToString(t));
        frame.set(self, NullSlot);
        break;
    }
}

void __builtin__inv__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode t = frame.typeAt(nargs[0]);
    switch (t) {
    case TypeCode::Int: {
        Int v = frame.get<Int>(nargs[0]);
        frame.set(self, ~v);
        break;
    }

    case TypeCode::Long: {
        Long v = frame.get<Long>(nargs[0]);
        frame.set(self, ~v);
        break;
    }

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(
                "<inv> operator only supported for integer types (Int / Long), got " +
                typeCodeToString(t));
        frame.set(self, NullSlot);
        break;
    }
}
