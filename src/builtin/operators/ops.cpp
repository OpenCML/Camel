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
 * Updated: Oct. 24, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "ops.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"
#include "utils/assert.h"

#include <cmath>

namespace GIR = GraphIR;

namespace {

using BinaryOpFunc = std::function<data_ptr_t(
    const data_ptr_t &, const data_ptr_t &, const type_ptr_t &, Context &)>;

template <typename TData, typename TValue>
data_ptr_t make_data(const type_ptr_t &type, TValue value) {
    return std::make_shared<TData>(value);
}

template <typename TData>
std::shared_ptr<TData> cast(const data_ptr_t &data, const type_ptr_t &type) {
    return data->as<TData>(type);
}

OperatorReturnCode eval_assignment_op(
    GIR::node_ptr_t &self, Frame &frame, Context &ctx, const std::string &opname,
    const BinaryOpFunc &op_func, bool require_integral = false, bool check_div_zero = false) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, opname + " requires exactly two arguments");

    const data_ptr_t &lhs = frame.get(ins[0]->index());
    const data_ptr_t &rhs = frame.get(ins[1]->index());

    const type_ptr_t &lhs_type = lhs->type();
    const type_ptr_t &rhs_type = rhs->type();

    if (require_integral &&
        (!(lhs_type->code() == TypeCode::Int32) || !(rhs_type->code() == TypeCode::Int64))) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<" + opname + "> requires integer types");
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    if (check_div_zero && rhs->isZero()) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<" + opname + "> division by zero");
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    data_ptr_t result = op_func(lhs, rhs, lhs_type, ctx);
    if (!result) {
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(ins[0]->index(), result);
    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

data_ptr_t
do_add(const data_ptr_t &lhs, const data_ptr_t &rhs, const type_ptr_t &type, Context &ctx) {
    if (type == Type::Int32()) {
        return make_data<Int32Data>(
            type,
            cast<Int32Data>(lhs, type)->data() + cast<Int32Data>(rhs, type)->data());
    } else if (type == Type::Int64()) {
        return make_data<Int64Data>(
            type,
            cast<Int64Data>(lhs, type)->data() + cast<Int64Data>(rhs, type)->data());
    } else if (type == Type::Float()) {
        return make_data<FloatData>(
            type,
            cast<FloatData>(lhs, type)->data() + cast<FloatData>(rhs, type)->data());
    } else if (type == Type::Double()) {
        return make_data<DoubleData>(
            type,
            cast<DoubleData>(lhs, type)->data() + cast<DoubleData>(rhs, type)->data());
    } else if (type == Type::String()) {
        return make_data<StringData>(
            type,
            cast<StringData>(lhs, type)->data() + cast<StringData>(rhs, type)->data());
    }
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("<add> operator not supported for type " + type->toString());
    return nullptr;
}

data_ptr_t
do_sub(const data_ptr_t &lhs, const data_ptr_t &rhs, const type_ptr_t &type, Context &ctx) {
    if (type == Type::Int32()) {
        return make_data<Int32Data>(
            type,
            cast<Int32Data>(lhs, type)->data() - cast<Int32Data>(rhs, type)->data());
    } else if (type == Type::Int64()) {
        return make_data<Int64Data>(
            type,
            cast<Int64Data>(lhs, type)->data() - cast<Int64Data>(rhs, type)->data());
    } else if (type == Type::Float()) {
        return make_data<FloatData>(
            type,
            cast<FloatData>(lhs, type)->data() - cast<FloatData>(rhs, type)->data());
    } else if (type == Type::Double()) {
        return make_data<DoubleData>(
            type,
            cast<DoubleData>(lhs, type)->data() - cast<DoubleData>(rhs, type)->data());
    }
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("<sub> operator not supported for type " + type->toString());
    return nullptr;
}

data_ptr_t
do_mul(const data_ptr_t &lhs, const data_ptr_t &rhs, const type_ptr_t &type, Context &ctx) {
    if (type == Type::Int32()) {
        return make_data<Int32Data>(
            type,
            cast<Int32Data>(lhs, type)->data() * cast<Int32Data>(rhs, type)->data());
    } else if (type == Type::Int64()) {
        return make_data<Int64Data>(
            type,
            cast<Int64Data>(lhs, type)->data() * cast<Int64Data>(rhs, type)->data());
    } else if (type == Type::Float()) {
        return make_data<FloatData>(
            type,
            cast<FloatData>(lhs, type)->data() * cast<FloatData>(rhs, type)->data());
    } else if (type == Type::Double()) {
        return make_data<DoubleData>(
            type,
            cast<DoubleData>(lhs, type)->data() * cast<DoubleData>(rhs, type)->data());
    }
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("<mul> operator not supported for type " + type->toString());
    return nullptr;
}

data_ptr_t
do_div(const data_ptr_t &lhs, const data_ptr_t &rhs, const type_ptr_t &type, Context &ctx) {
    if (type == Type::Int32()) {
        return make_data<Int32Data>(
            type,
            cast<Int32Data>(lhs, type)->data() / cast<Int32Data>(rhs, type)->data());
    } else if (type == Type::Int64()) {
        return make_data<Int64Data>(
            type,
            cast<Int64Data>(lhs, type)->data() / cast<Int64Data>(rhs, type)->data());
    } else if (type == Type::Float()) {
        return make_data<FloatData>(
            type,
            cast<FloatData>(lhs, type)->data() / cast<FloatData>(rhs, type)->data());
    } else if (type == Type::Double()) {
        return make_data<DoubleData>(
            type,
            cast<DoubleData>(lhs, type)->data() / cast<DoubleData>(rhs, type)->data());
    }
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("<div> operator not supported for type " + type->toString());
    return nullptr;
}

data_ptr_t
do_mod(const data_ptr_t &lhs, const data_ptr_t &rhs, const type_ptr_t &type, Context &ctx) {
    if (type == Type::Int32()) {
        return make_data<Int32Data>(
            type,
            cast<Int32Data>(lhs, type)->data() % cast<Int32Data>(rhs, type)->data());
    } else if (type == Type::Int64()) {
        return make_data<Int64Data>(
            type,
            cast<Int64Data>(lhs, type)->data() % cast<Int64Data>(rhs, type)->data());
    }
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("<mod> operator not supported for type " + type->toString());
    return nullptr;
}

data_ptr_t
do_pow(const data_ptr_t &lhs, const data_ptr_t &rhs, const type_ptr_t &type, Context &ctx) {
    if (type == Type::Float()) {
        return make_data<FloatData>(
            type,
            std::pow(cast<FloatData>(lhs, type)->data(), cast<FloatData>(rhs, type)->data()));
    } else if (type == Type::Double()) {
        return make_data<DoubleData>(
            type,
            std::pow(cast<DoubleData>(lhs, type)->data(), cast<DoubleData>(rhs, type)->data()));
    }
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("<pow> operator not supported for type " + type->toString());
    return nullptr;
}

data_ptr_t
do_and(const data_ptr_t &lhs, const data_ptr_t &rhs, const type_ptr_t &type, Context &ctx) {
    if (type == Type::Int32()) {
        return make_data<Int32Data>(
            type,
            cast<Int32Data>(lhs, type)->data() & cast<Int32Data>(rhs, type)->data());
    } else if (type == Type::Int64()) {
        return make_data<Int64Data>(
            type,
            cast<Int64Data>(lhs, type)->data() & cast<Int64Data>(rhs, type)->data());
    }
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("<and> operator not supported for type " + type->toString());
    return nullptr;
}

data_ptr_t
do_or(const data_ptr_t &lhs, const data_ptr_t &rhs, const type_ptr_t &type, Context &ctx) {
    if (type == Type::Int32()) {
        return make_data<Int32Data>(
            type,
            cast<Int32Data>(lhs, type)->data() | cast<Int32Data>(rhs, type)->data());
    } else if (type == Type::Int64()) {
        return make_data<Int64Data>(
            type,
            cast<Int64Data>(lhs, type)->data() | cast<Int64Data>(rhs, type)->data());
    }
    ctx.rtmDiags()
        ->of(RuntimeDiag::RuntimeError)
        .commit("<or> operator not supported for type " + type->toString());
    return nullptr;
}

} // anonymous namespace

OperatorReturnCode __builtin__assn__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "assn requires exactly two arguments");
    const data_ptr_t &rhs = frame.get(ins[1]->index());
    frame.set(ins[0]->index(), rhs);
    frame.set(self->index(), rhs);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__assn_add__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    return eval_assignment_op(self, frame, ctx, "assn_add", do_add);
}

OperatorReturnCode __builtin__assn_sub__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    return eval_assignment_op(self, frame, ctx, "assn_sub", do_sub);
}

OperatorReturnCode __builtin__assn_mul__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    return eval_assignment_op(self, frame, ctx, "assn_mul", do_mul);
}

OperatorReturnCode __builtin__assn_div__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    return eval_assignment_op(self, frame, ctx, "assn_div", do_div, false, true);
}

OperatorReturnCode __builtin__assn_mod__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    return eval_assignment_op(self, frame, ctx, "assn_mod", do_mod, false, true);
}

OperatorReturnCode __builtin__assn_pow__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    return eval_assignment_op(self, frame, ctx, "assn_pow", do_pow);
}

OperatorReturnCode __builtin__assn_and__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    return eval_assignment_op(self, frame, ctx, "assn_and", do_and, true);
}

OperatorReturnCode __builtin__assn_or__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    return eval_assignment_op(self, frame, ctx, "assn_or", do_or, true);
}

OperatorReturnCode __builtin__assn_mat__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<assn_mat> not implemented");
    frame.set(self->index(), Data::null());
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__or__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "or operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    auto l = left->as<BoolData>(Type::Bool());
    if (l->data()) {
        frame.set(self->index(), std::make_shared<BoolData>(true));
    } else {
        auto r = right->as<BoolData>(Type::Bool());
        frame.set(self->index(), std::make_shared<BoolData>(r->data()));
    }
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__and__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "and operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    auto l = left->as<BoolData>(Type::Bool());
    if (!l->data()) {
        frame.set(self->index(), std::make_shared<BoolData>(false));
    } else {
        auto r = right->as<BoolData>(Type::Bool());
        frame.set(self->index(), std::make_shared<BoolData>(r->data()));
    }
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__eq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "eq operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    bool res = left->equals(right);
    frame.set(self->index(), std::make_shared<BoolData>(res));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__neq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "neq operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    bool res = !left->equals(right);
    frame.set(self->index(), std::make_shared<BoolData>(res));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__strict_eq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "strict_eq operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    bool res = left->equals(right);
    frame.set(self->index(), std::make_shared<BoolData>(res));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__strict_neq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "strict_neq operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    bool res = !left->equals(right);
    frame.set(self->index(), std::make_shared<BoolData>(res));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__lt__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "lt operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    frame.set(self->index(), std::make_shared<BoolData>(l->data() < r->data()));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__le__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "le operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    frame.set(self->index(), std::make_shared<BoolData>(l->data() <= r->data()));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__gt__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "gt operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    frame.set(self->index(), std::make_shared<BoolData>(l->data() > r->data()));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__ge__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "ge operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    frame.set(self->index(), std::make_shared<BoolData>(l->data() >= r->data()));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__add_ii__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    data_ptr_t result;
    result = std::make_shared<Int32Data>(
        left->as<Int32Data>(Type::Int32())->data() + right->as<Int32Data>(Type::Int32())->data());

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__add_ll__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    data_ptr_t result;
    result = std::make_shared<Int64Data>(
        left->as<Int64Data>(Type::Int64())->data() + right->as<Int64Data>(Type::Int64())->data());

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__add_ff__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    data_ptr_t result;
    result = std::make_shared<FloatData>(
        left->as<FloatData>(Type::Float())->data() + right->as<FloatData>(Type::Float())->data());

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__add_dd__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    data_ptr_t result;
    result = std::make_shared<DoubleData>(
        left->as<DoubleData>(Type::Double())->data() +
        right->as<DoubleData>(Type::Double())->data());

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__add_ss__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    data_ptr_t result;
    result = std::make_shared<DoubleData>(
        left->as<DoubleData>(Type::Double())->data() +
        right->as<DoubleData>(Type::Double())->data());

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__sub_ii__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    int32_t lval = left->as<Int32Data>(Type::Int32())->data();
    int32_t rval = right->as<Int32Data>(Type::Int32())->data();

    data_ptr_t result = std::make_shared<Int32Data>(lval - rval);
    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__sub_ll__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    int64_t lval = left->as<Int64Data>(Type::Int64())->data();
    int64_t rval = right->as<Int64Data>(Type::Int64())->data();

    data_ptr_t result = std::make_shared<Int64Data>(lval - rval);
    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__sub_ff__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    float lval = left->as<FloatData>(Type::Float())->data();
    float rval = right->as<FloatData>(Type::Float())->data();

    data_ptr_t result = std::make_shared<FloatData>(lval - rval);
    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__sub_dd__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    double lval = left->as<DoubleData>(Type::Double())->data();
    double rval = right->as<DoubleData>(Type::Double())->data();

    data_ptr_t result = std::make_shared<DoubleData>(lval - rval);
    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__mul__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "mul operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

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
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__div__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "div operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    // 除以 0 检查
    if ((right->type() == Type::Int32() && right->as<Int32Data>(Type::Int32())->data() == 0) ||
        (right->type() == Type::Int64() && right->as<Int64Data>(Type::Int64())->data() == 0) ||
        (right->type() == Type::Float() && right->as<FloatData>(Type::Float())->data() == 0.0f) ||
        (right->type() == Type::Double() && right->as<DoubleData>(Type::Double())->data() == 0.0)) {
        ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
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
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__mod__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "mod operator requires exactly two arguments");

    const data_ptr_t &left = frame.get(ins[0]->index());
    const data_ptr_t &right = frame.get(ins[1]->index());

    if ((right->type() == Type::Int32() && right->as<Int32Data>(Type::Int32())->data() == 0) ||
        (right->type() == Type::Int64() && right->as<Int64Data>(Type::Int64())->data() == 0)) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<mod> division by zero");
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
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
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__mat__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "mat operator not implemented");
    frame.set(self->index(), Data::null());
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__pow__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "pow operator requires exactly two arguments");

    const data_ptr_t &base = frame.get(ins[0]->index());
    const data_ptr_t &exponent = frame.get(ins[1]->index());

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
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__idx__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 2, "idx operator requires exactly two arguments");

    const data_ptr_t &container = frame.get(ins[0]->index());
    const data_ptr_t &index = frame.get(ins[1]->index());

    // 数值索引：适用于 Array
    if (index->type() == Type::Int32()) {
        size_t idx = static_cast<size_t>(index->as<Int32Data>(Type::Int32())->data());
        auto arr = std::dynamic_pointer_cast<ArrayData>(container);
        ASSERT(idx < arr->raw().size(), "Array index out of bounds.");
        frame.set(self->index(), arr->raw()[idx]);

        return OperatorReturnCode::OK;
    }

    // 字符串索引：适用于 Struct
    if (index->type() == Type::String()) {
        auto dict = std::dynamic_pointer_cast<StructData>(container);
        const std::string &key = index->as<StringData>(Type::String())->data();
        const auto &map = dict->raw();

        auto it = map.find(key);
        ASSERT(it != map.end(), "Struct key not found: " + key);
        frame.set(self->index(), it->second);

        return OperatorReturnCode::OK;
    }

    frame.set(self->index(), Data::null());
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__not__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 1, "not operator requires exactly one argument");

    const data_ptr_t &val = frame.get(ins[0]->index());

    bool result = !val->as<BoolData>(Type::Bool())->data();
    frame.set(self->index(), std::make_shared<BoolData>(result));
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__neg__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 1, "neg operator requires exactly one argument");

    const data_ptr_t &val = frame.get(ins[0]->index());

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
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}

OperatorReturnCode __builtin__inv__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 1, "inv operator requires exactly one argument");

    const data_ptr_t &val = frame.get(ins[0]->index());

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
        frame.set(self->index(), Data::null());
        return OperatorReturnCode::OK;
    }

    frame.set(self->index(), result);
    return OperatorReturnCode::OK;
}
