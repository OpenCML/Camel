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
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "ops.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"
#include "utils/assert.h"

#include <cmath>

EvalResultCode __builtin__assn__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__assn_add__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn_add operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__assn_sub__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn_sub operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__assn_mul__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn_mul operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__assn_div__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn_div operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__assn_mod__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn_mod operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__assn_mat__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn_mat operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__assn_exp__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn_exp operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__assn_and__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn_and operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__assn_or__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "assn_or operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__or__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "or operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    if (!Type::castSafetyCheck(left->type(), Type::Bool())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<or>", "bool", left->type()->toString());
        return Data::null();
    }
    if (!Type::castSafetyCheck(right->type(), Type::Bool())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<or>", "bool", right->type()->toString());
        return Data::null();
    }
    auto l = left->as<BoolData>(Type::Bool());
    if (l->data()) {
        return std::make_shared<BoolData>(true);
    } else {
        auto r = right->as<BoolData>(Type::Bool());
        return std::make_shared<BoolData>(r->data());
    }
}

EvalResultCode __builtin__and__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "and operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    if (!Type::castSafetyCheck(left->type(), Type::Bool())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<and>", "bool", left->type()->toString());
        return Data::null();
    }
    if (!Type::castSafetyCheck(right->type(), Type::Bool())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<and>", "bool", right->type()->toString());
        return Data::null();
    }
    auto l = left->as<BoolData>(Type::Bool());
    if (!l->data()) {
        return std::make_shared<BoolData>(false);
    } else {
        auto r = right->as<BoolData>(Type::Bool());
        return std::make_shared<BoolData>(r->data());
    }
}

EvalResultCode __builtin__eq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "eq operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    bool res = left->equals(right);
    return std::make_shared<BoolData>(res);
}

EvalResultCode __builtin__neq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "neq operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    bool res = !left->equals(right);
    return std::make_shared<BoolData>(res);
}

EvalResultCode __builtin__strict_eq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "strict_eq operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    // TODO: implement strict equality check
    bool res = left->equals(right);
    return std::make_shared<BoolData>(res);
}

EvalResultCode __builtin__strict_neq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "strict_neq operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    // TODO: implement strict inequality check
    bool res = !left->equals(right);
    return std::make_shared<BoolData>(res);
}

EvalResultCode __builtin__lt__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "lt operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    if (!Type::castSafetyCheck(left->type(), Type::Double())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<lt>", "double", left->type()->toString());
        return std::make_shared<BoolData>(false);
    }
    if (!Type::castSafetyCheck(right->type(), Type::Double())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<lt>", "double", right->type()->toString());
        return std::make_shared<BoolData>(false);
    }

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    return std::make_shared<BoolData>(l->data() < r->data());
}

EvalResultCode __builtin__le__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "le operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    if (!Type::castSafetyCheck(left->type(), Type::Double())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<le>", "double", left->type()->toString());
        return std::make_shared<BoolData>(false);
    }
    if (!Type::castSafetyCheck(right->type(), Type::Double())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<le>", "double", right->type()->toString());
        return std::make_shared<BoolData>(false);
    }

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    return std::make_shared<BoolData>(l->data() <= r->data());
}

EvalResultCode __builtin__gt__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "gt operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    if (!Type::castSafetyCheck(left->type(), Type::Double())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<gt>", "double", left->type()->toString());
        return std::make_shared<BoolData>(false);
    }
    if (!Type::castSafetyCheck(right->type(), Type::Double())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<gt>", "double", right->type()->toString());
        return std::make_shared<BoolData>(false);
    }

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    return std::make_shared<BoolData>(l->data() > r->data());
}

EvalResultCode __builtin__ge__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "ge operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    if (!Type::castSafetyCheck(left->type(), Type::Double())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(0, "<ge>", "double", left->type()->toString());
        return std::make_shared<BoolData>(false);
    }
    if (!Type::castSafetyCheck(right->type(), Type::Double())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<ge>", "double", right->type()->toString());
        return std::make_shared<BoolData>(false);
    }

    auto l = left->as<DoubleData>(Type::Double());
    auto r = right->as<DoubleData>(Type::Double());

    return std::make_shared<BoolData>(l->data() >= r->data());
}

EvalResultCode __builtin__add__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "add operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];
    if (!left->type()->primary() || !right->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<add> operator requires primary types");
        return Data::null();
    }
    // 检查第二个参数是否可以转换为第一个参数的类型
    if (!Type::castSafetyCheck(right->type(), left->type())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<add>", left->type()->toString(), right->type()->toString());
        return Data::null();
    }
    // 根据第一个参数的类型分别处理
    if (left->type() == Type::Int32()) {
        auto l = left->as<Int32Data>(Type::Int32());
        auto r = right->as<Int32Data>(Type::Int32());
        return std::make_shared<Int32Data>(l->data() + r->data());
    } else if (left->type() == Type::Int64()) {
        auto l = left->as<Int64Data>(Type::Int64());
        auto r = right->as<Int64Data>(Type::Int64());
        return std::make_shared<Int64Data>(l->data() + r->data());
    } else if (left->type() == Type::Float()) {
        auto l = left->as<FloatData>(Type::Float());
        auto r = right->as<FloatData>(Type::Float());
        return std::make_shared<FloatData>(l->data() + r->data());
    } else if (left->type() == Type::Double()) {
        auto l = left->as<DoubleData>(Type::Double());
        auto r = right->as<DoubleData>(Type::Double());
        return std::make_shared<DoubleData>(l->data() + r->data());
    } else if (left->type() == Type::String()) {
        auto l = left->as<StringData>(Type::String());
        auto r = right->as<StringData>(Type::String());
        return std::make_shared<StringData>(l->data() + r->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<add> operator not supported for type " + left->type()->toString());
        return Data::null();
    }
}

EvalResultCode __builtin__sub__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "sub operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];

    if (!left->type()->primary() || !right->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sub> operator requires primary types");
        return Data::null();
    }

    if (!Type::castSafetyCheck(right->type(), left->type())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<sub>", left->type()->toString(), right->type()->toString());
        return Data::null();
    }

    if (left->type() == Type::Int32()) {
        auto l = left->as<Int32Data>(Type::Int32());
        auto r = right->as<Int32Data>(Type::Int32());
        return std::make_shared<Int32Data>(l->data() - r->data());
    } else if (left->type() == Type::Int64()) {
        auto l = left->as<Int64Data>(Type::Int64());
        auto r = right->as<Int64Data>(Type::Int64());
        return std::make_shared<Int64Data>(l->data() - r->data());
    } else if (left->type() == Type::Float()) {
        auto l = left->as<FloatData>(Type::Float());
        auto r = right->as<FloatData>(Type::Float());
        return std::make_shared<FloatData>(l->data() - r->data());
    } else if (left->type() == Type::Double()) {
        auto l = left->as<DoubleData>(Type::Double());
        auto r = right->as<DoubleData>(Type::Double());
        return std::make_shared<DoubleData>(l->data() - r->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sub> operator not supported for type " + left->type()->toString());
        return Data::null();
    }
}

EvalResultCode __builtin__mul__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "mul operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];

    if (!left->type()->primary() || !right->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mul> operator requires primary types");
        return Data::null();
    }

    if (!Type::castSafetyCheck(right->type(), left->type())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<mul>", left->type()->toString(), right->type()->toString());
        return Data::null();
    }

    if (left->type() == Type::Int32()) {
        auto l = left->as<Int32Data>(Type::Int32());
        auto r = right->as<Int32Data>(Type::Int32());
        return std::make_shared<Int32Data>(l->data() * r->data());
    } else if (left->type() == Type::Int64()) {
        auto l = left->as<Int64Data>(Type::Int64());
        auto r = right->as<Int64Data>(Type::Int64());
        return std::make_shared<Int64Data>(l->data() * r->data());
    } else if (left->type() == Type::Float()) {
        auto l = left->as<FloatData>(Type::Float());
        auto r = right->as<FloatData>(Type::Float());
        return std::make_shared<FloatData>(l->data() * r->data());
    } else if (left->type() == Type::Double()) {
        auto l = left->as<DoubleData>(Type::Double());
        auto r = right->as<DoubleData>(Type::Double());
        return std::make_shared<DoubleData>(l->data() * r->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mul> operator not supported for type " + left->type()->toString());
        return Data::null();
    }
}

EvalResultCode __builtin__div__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "div operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];

    if (!left->type()->primary() || !right->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<div> operator requires primary types");
        return Data::null();
    }

    if (!Type::castSafetyCheck(right->type(), left->type())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<div>", left->type()->toString(), right->type()->toString());
        return Data::null();
    }

    // 检查除以 0
    if ((right->type() == Type::Int32() && right->as<Int32Data>(Type::Int32())->data() == 0) ||
        (right->type() == Type::Int64() && right->as<Int64Data>(Type::Int64())->data() == 0) ||
        (right->type() == Type::Float() && right->as<FloatData>(Type::Float())->data() == 0.0f) ||
        (right->type() == Type::Double() && right->as<DoubleData>(Type::Double())->data() == 0.0)) {
        ctx.rtmDiags()->of(RuntimeDiag::DivisionByZero).commit();
        return Data::null();
    }

    if (left->type() == Type::Int32()) {
        auto l = left->as<Int32Data>(Type::Int32());
        auto r = right->as<Int32Data>(Type::Int32());
        return std::make_shared<Int32Data>(l->data() / r->data());
    } else if (left->type() == Type::Int64()) {
        auto l = left->as<Int64Data>(Type::Int64());
        auto r = right->as<Int64Data>(Type::Int64());
        return std::make_shared<Int64Data>(l->data() / r->data());
    } else if (left->type() == Type::Float()) {
        auto l = left->as<FloatData>(Type::Float());
        auto r = right->as<FloatData>(Type::Float());
        return std::make_shared<FloatData>(l->data() / r->data());
    } else if (left->type() == Type::Double()) {
        auto l = left->as<DoubleData>(Type::Double());
        auto r = right->as<DoubleData>(Type::Double());
        return std::make_shared<DoubleData>(l->data() / r->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<div> operator not supported for type " + left->type()->toString());
        return Data::null();
    }
}

EvalResultCode __builtin__mod__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "mod operator requires exactly two arguments");
    auto left = norm[0];
    auto right = norm[1];

    if (!left->type()->primary() || !right->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mod> operator requires primary types");
        return Data::null();
    }

    if (!Type::castSafetyCheck(right->type(), left->type())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<mod>", left->type()->toString(), right->type()->toString());
        return Data::null();
    }

    if ((right->type() == Type::Int32() && right->as<Int32Data>(Type::Int32())->data() == 0) ||
        (right->type() == Type::Int64() && right->as<Int64Data>(Type::Int64())->data() == 0)) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<mod> division by zero");
        return Data::null();
    }

    if (left->type() == Type::Int32()) {
        auto l = left->as<Int32Data>(Type::Int32());
        auto r = right->as<Int32Data>(Type::Int32());
        return std::make_shared<Int32Data>(l->data() % r->data());
    } else if (left->type() == Type::Int64()) {
        auto l = left->as<Int64Data>(Type::Int64());
        auto r = right->as<Int64Data>(Type::Int64());
        return std::make_shared<Int64Data>(l->data() % r->data());
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<mod> operator only supports integer types");
        return Data::null();
    }
}

EvalResultCode __builtin__mat__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "mat operator not implemented");
    return nullptr;
}

EvalResultCode __builtin__exp__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 2, "exp operator requires exactly two arguments");
    auto base = norm[0];
    auto exponent = norm[1];

    if (!base->type()->primary() || !exponent->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<exp> operator requires primary types");
        return Data::null();
    }

    if (!Type::castSafetyCheck(exponent->type(), base->type())) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::IncompatibleArgType)
            .commit(1, "<exp>", base->type()->toString(), exponent->type()->toString());
        return Data::null();
    }

    if (base->type() == Type::Int32()) {
        auto b = base->as<Int32Data>(Type::Int32());
        auto e = exponent->as<Int32Data>(Type::Int32());
        return std::make_shared<Int32Data>(static_cast<int32_t>(std::pow(b->data(), e->data())));
    } else if (base->type() == Type::Int64()) {
        auto b = base->as<Int64Data>(Type::Int64());
        auto e = exponent->as<Int64Data>(Type::Int64());
        return std::make_shared<Int64Data>(static_cast<int64_t>(std::pow(b->data(), e->data())));
    } else if (base->type() == Type::Float()) {
        auto b = base->as<FloatData>(Type::Float());
        auto e = exponent->as<FloatData>(Type::Float());
        return std::make_shared<FloatData>(std::pow(b->data(), e->data()));
    } else if (base->type() == Type::Double()) {
        auto b = base->as<DoubleData>(Type::Double());
        auto e = exponent->as<DoubleData>(Type::Double());
        return std::make_shared<DoubleData>(std::pow(b->data(), e->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<exp> operator not supported for type " + base->type()->toString());
        return Data::null();
    }
}

EvalResultCode __builtin__idx__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(false, "idx operator not implemented");
    return nullptr;
}