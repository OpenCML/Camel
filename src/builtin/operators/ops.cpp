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
 * Updated: Oct. 26, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "ops.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"
#include "utils/assert.h"

#include <cmath>

namespace GIR = GraphIR;

void __builtin__assn__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &rhs = frame.get(nargs[1]);
    frame.set(self, rhs);
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
}

void __builtin__assn_add_i__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<Int32Data>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<Int32Data>(frame.get(nargs[1]));
    int32_t res = lhs->data() + rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_add_l__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<Int64Data>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<Int64Data>(frame.get(nargs[1]));
    int64_t res = lhs->data() + rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_add_f__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<FloatData>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<FloatData>(frame.get(nargs[1]));
    float res = lhs->data() + rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_add_d__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<DoubleData>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<DoubleData>(frame.get(nargs[1]));
    double res = lhs->data() + rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_sub_i__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<Int32Data>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<Int32Data>(frame.get(nargs[1]));
    int32_t res = lhs->data() - rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_sub_l__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<Int64Data>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<Int64Data>(frame.get(nargs[1]));
    int64_t res = lhs->data() - rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_sub_f__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<FloatData>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<FloatData>(frame.get(nargs[1]));
    float res = lhs->data() - rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_sub_d__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<DoubleData>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<DoubleData>(frame.get(nargs[1]));
    double res = lhs->data() - rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_mul_i__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<Int32Data>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<Int32Data>(frame.get(nargs[1]));
    int32_t res = lhs->data() * rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_mul_l__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<Int64Data>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<Int64Data>(frame.get(nargs[1]));
    int64_t res = lhs->data() * rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_mul_f__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<FloatData>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<FloatData>(frame.get(nargs[1]));
    float res = lhs->data() * rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_mul_d__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<DoubleData>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<DoubleData>(frame.get(nargs[1]));
    double res = lhs->data() * rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_div_i__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<Int32Data>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<Int32Data>(frame.get(nargs[1]));
    if (rhs->data() == 0)
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
    int32_t res = lhs->data() / rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_div_l__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<Int64Data>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<Int64Data>(frame.get(nargs[1]));
    if (rhs->data() == 0)
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
    int64_t res = lhs->data() / rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_div_f__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<FloatData>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<FloatData>(frame.get(nargs[1]));
    if (rhs->data() == 0.0f)
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
    float res = lhs->data() / rhs->data();
    lhs->data() = res;
    frame.set(self, lhs);
}

void __builtin__assn_div_d__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const auto &lhs = tt::as_shared<DoubleData>(frame.get(nargs[0]));
    const auto &rhs = tt::as_shared<DoubleData>(frame.get(nargs[1]));
    if (rhs->data() == 0.0)
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("Division by zero");
    double res = lhs->data() / rhs->data();
    lhs->data() = res;
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
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("assignment operator not fully implemented");
}

void __builtin__or__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    auto l = left->as<BoolData>(Type::Bool());
    if (l->data()) {
        frame.set(self, std::make_shared<BoolData>(true));
    } else {
        auto r = right->as<BoolData>(Type::Bool());
        frame.set(self, std::make_shared<BoolData>(r->data()));
    }
}

void __builtin__and__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    auto l = left->as<BoolData>(Type::Bool());
    if (!l->data()) {
        frame.set(self, std::make_shared<BoolData>(false));
    } else {
        auto r = right->as<BoolData>(Type::Bool());
        frame.set(self, std::make_shared<BoolData>(r->data()));
    }
}

void __builtin__eq__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    bool res = left->equals(right);
    frame.set(self, std::make_shared<BoolData>(res));
}

void __builtin__ne__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    bool res = !left->equals(right);
    frame.set(self, std::make_shared<BoolData>(res));
}

void __builtin__strict_eq__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    bool res = left->equals(right);
    frame.set(self, std::make_shared<BoolData>(res));
}

void __builtin__strict_ne__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    bool res = !left->equals(right);
    frame.set(self, std::make_shared<BoolData>(res));
}

void __builtin__lt__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    frame.set(self, std::make_shared<BoolData>(l->data() < r->data()));
}

void __builtin__le__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    frame.set(self, std::make_shared<BoolData>(l->data() <= r->data()));
}

void __builtin__gt__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    frame.set(self, std::make_shared<BoolData>(l->data() > r->data()));
}

void __builtin__ge__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    frame.set(self, std::make_shared<BoolData>(l->data() >= r->data()));
}

void __builtin__add_ii__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    data_ptr_t result;
    result = std::make_shared<Int32Data>(
        left->as<Int32Data>(Type::Int32())->data() + right->as<Int32Data>(Type::Int32())->data());

    frame.set(self, result);
}

void __builtin__add_ll__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    data_ptr_t result;
    result = std::make_shared<Int64Data>(
        left->as<Int64Data>(Type::Int64())->data() + right->as<Int64Data>(Type::Int64())->data());

    frame.set(self, result);
}

void __builtin__add_ff__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    data_ptr_t result;
    result = std::make_shared<FloatData>(
        left->as<FloatData>(Type::Float())->data() + right->as<FloatData>(Type::Float())->data());

    frame.set(self, result);
}

void __builtin__add_dd__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    data_ptr_t result;
    result = std::make_shared<DoubleData>(
        left->as<DoubleData>(Type::Double())->data() +
        right->as<DoubleData>(Type::Double())->data());

    frame.set(self, result);
}

void __builtin__add_ss__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    data_ptr_t result;
    result = std::make_shared<DoubleData>(
        left->as<DoubleData>(Type::Double())->data() +
        right->as<DoubleData>(Type::Double())->data());

    frame.set(self, result);
}

void __builtin__sub_ii__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    int32_t lval = left->as<Int32Data>(Type::Int32())->data();
    int32_t rval = right->as<Int32Data>(Type::Int32())->data();

    data_ptr_t result = std::make_shared<Int32Data>(lval - rval);
    frame.set(self, result);
}

void __builtin__sub_ll__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    int64_t lval = left->as<Int64Data>(Type::Int64())->data();
    int64_t rval = right->as<Int64Data>(Type::Int64())->data();

    data_ptr_t result = std::make_shared<Int64Data>(lval - rval);
    frame.set(self, result);
}

void __builtin__sub_ff__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    float lval = left->as<FloatData>(Type::Float())->data();
    float rval = right->as<FloatData>(Type::Float())->data();

    data_ptr_t result = std::make_shared<FloatData>(lval - rval);
    frame.set(self, result);
}

void __builtin__sub_dd__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    double lval = left->as<DoubleData>(Type::Double())->data();
    double rval = right->as<DoubleData>(Type::Double())->data();

    data_ptr_t result = std::make_shared<DoubleData>(lval - rval);
    frame.set(self, result);
}

void __builtin__mul__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    data_ptr_t result;
    if (left->type() == Type::Int32()) {
        result = std::make_shared<Int32Data>(
            left->as<Int32Data>(Type::Int32())->data() *
            right->as<Int32Data>(Type::Int32())->data());
    } else if (left->type() == Type::Int64()) {
        result = std::make_shared<Int64Data>(
            left->as<Int64Data>(Type::Int64())->data() *
            right->as<Int64Data>(Type::Int64())->data());
    } else if (left->type() == Type::Float()) {
        result = std::make_shared<FloatData>(
            left->as<FloatData>(Type::Float())->data() *
            right->as<FloatData>(Type::Float())->data());
    } else if (left->type() == Type::Double()) {
        result = std::make_shared<DoubleData>(
            left->as<DoubleData>(Type::Double())->data() *
            right->as<DoubleData>(Type::Double())->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mul> operator not supported for type " + left->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __builtin__div__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    // 除以 0 检查
    if ((right->type() == Type::Int32() && right->as<Int32Data>(Type::Int32())->data() == 0) ||
        (right->type() == Type::Int64() && right->as<Int64Data>(Type::Int64())->data() == 0) ||
        (right->type() == Type::Float() && right->as<FloatData>(Type::Float())->data() == 0.0f) ||
        (right->type() == Type::Double() && right->as<DoubleData>(Type::Double())->data() == 0.0)) {
        ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
        frame.set(self, Data::null());
        return;
    }

    data_ptr_t result;
    if (left->type() == Type::Int32()) {
        result = std::make_shared<Int32Data>(
            left->as<Int32Data>(Type::Int32())->data() /
            right->as<Int32Data>(Type::Int32())->data());
    } else if (left->type() == Type::Int64()) {
        result = std::make_shared<Int64Data>(
            left->as<Int64Data>(Type::Int64())->data() /
            right->as<Int64Data>(Type::Int64())->data());
    } else if (left->type() == Type::Float()) {
        result = std::make_shared<FloatData>(
            left->as<FloatData>(Type::Float())->data() /
            right->as<FloatData>(Type::Float())->data());
    } else if (left->type() == Type::Double()) {
        result = std::make_shared<DoubleData>(
            left->as<DoubleData>(Type::Double())->data() /
            right->as<DoubleData>(Type::Double())->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<div> operator not supported for type " + left->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __builtin__mod__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &left = frame.get(nargs[0]);
    const data_ptr_t &right = frame.get(nargs[1]);

    if ((right->type() == Type::Int32() && right->as<Int32Data>(Type::Int32())->data() == 0) ||
        (right->type() == Type::Int64() && right->as<Int64Data>(Type::Int64())->data() == 0)) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<mod> division by zero");
        frame.set(self, Data::null());
        return;
    }

    data_ptr_t result;
    if (left->type() == Type::Int32()) {
        result = std::make_shared<Int32Data>(
            left->as<Int32Data>(Type::Int32())->data() %
            right->as<Int32Data>(Type::Int32())->data());
    } else if (left->type() == Type::Int64()) {
        result = std::make_shared<Int64Data>(
            left->as<Int64Data>(Type::Int64())->data() %
            right->as<Int64Data>(Type::Int64())->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mod> operator only supports integer types");
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __builtin__mat__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    ASSERT(false, "mat operator not implemented");
}

void __builtin__pow__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &base = frame.get(nargs[0]);
    const data_ptr_t &exponent = frame.get(nargs[1]);

    data_ptr_t result;
    if (base->type() == Type::Int32()) {
        result = std::make_shared<Int32Data>(static_cast<int32_t>(std::pow(
            base->as<Int32Data>(Type::Int32())->data(),
            exponent->as<Int32Data>(Type::Int32())->data())));
    } else if (base->type() == Type::Int64()) {
        result = std::make_shared<Int64Data>(static_cast<int64_t>(std::pow(
            base->as<Int64Data>(Type::Int64())->data(),
            exponent->as<Int64Data>(Type::Int64())->data())));
    } else if (base->type() == Type::Float()) {
        result = std::make_shared<FloatData>(std::pow(
            base->as<FloatData>(Type::Float())->data(),
            exponent->as<FloatData>(Type::Float())->data()));
    } else if (base->type() == Type::Double()) {
        result = std::make_shared<DoubleData>(std::pow(
            base->as<DoubleData>(Type::Double())->data(),
            exponent->as<DoubleData>(Type::Double())->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<pow> operator not supported for type " + base->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __builtin__idx__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &container = frame.get(nargs[0]);
    const data_ptr_t &index = frame.get(nargs[1]);

    // 数值索引：适用于 Array
    if (index->type() == Type::Int32()) {
        size_t idx = static_cast<size_t>(index->as<Int32Data>(Type::Int32())->data());
        auto arr = std::dynamic_pointer_cast<ArrayData>(container);
        ASSERT(idx < arr->raw().size(), "Array index out of bounds.");
        frame.set(self, arr->raw()[idx]);

        return;
    }

    // 字符串索引：适用于 Struct
    if (index->type() == Type::String()) {
        auto dict = std::dynamic_pointer_cast<StructData>(container);
        const std::string &key = index->as<StringData>(Type::String())->data();
        const auto &map = dict->raw();

        auto it = map.find(key);
        ASSERT(it != map.end(), "Struct key not found: " + key);
        frame.set(self, it->second);

        return;
    }

    frame.set(self, Data::null());
}

void __builtin__not__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &val = frame.get(nargs[0]);

    bool result = !val->as<BoolData>(Type::Bool())->data();
    frame.set(self, std::make_shared<BoolData>(result));
}

void __builtin__neg__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &val = frame.get(nargs[0]);

    data_ptr_t result;
    if (val->type() == Type::Int32()) {
        result = std::make_shared<Int32Data>(-val->as<Int32Data>(Type::Int32())->data());
    } else if (val->type() == Type::Int64()) {
        result = std::make_shared<Int64Data>(-val->as<Int64Data>(Type::Int64())->data());
    } else if (val->type() == Type::Float()) {
        result = std::make_shared<FloatData>(-val->as<FloatData>(Type::Float())->data());
    } else if (val->type() == Type::Double()) {
        result = std::make_shared<DoubleData>(-val->as<DoubleData>(Type::Double())->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<neg> operator not supported for type " + val->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __builtin__inv__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &val = frame.get(nargs[0]);

    data_ptr_t result;
    if (val->type() == Type::Int32()) {
        result = std::make_shared<Int32Data>(~val->as<Int32Data>(Type::Int32())->data());
    } else if (val->type() == Type::Int64()) {
        result = std::make_shared<Int64Data>(~val->as<Int64Data>(Type::Int64())->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit(
                "<inv> operator only supported for integer types (Int32 / Int64), got " +
                val->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}
