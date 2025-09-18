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

#include "math.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

EvalResultCode __abs__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 1, "abs operator requires exactly one argument");
    auto val = norm[0];
    if (!val->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<abs> operator requires a primary type");
        return Data::null();
    }

    if (val->type() == Type::Int32()) {
        auto v = val->as<Int32Data>(Type::Int32());
        return std::make_shared<Int32Data>(std::abs(v->data()));
    } else if (val->type() == Type::Int64()) {
        auto v = val->as<Int64Data>(Type::Int64());
        return std::make_shared<Int64Data>(std::abs(v->data()));
    } else if (val->type() == Type::Float()) {
        auto v = val->as<FloatData>(Type::Float());
        return std::make_shared<FloatData>(std::fabs(v->data()));
    } else if (val->type() == Type::Double()) {
        auto v = val->as<DoubleData>(Type::Double());
        return std::make_shared<DoubleData>(std::fabs(v->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<abs> not supported for type " + val->type()->toString());
        return Data::null();
    }
}

EvalResultCode __round__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 1, "round operator requires exactly one argument");
    auto val = norm[0];
    if (!val->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<round> operator requires a primary type");
        return Data::null();
    }

    if (val->type() == Type::Float()) {
        auto v = val->as<FloatData>(Type::Float());
        return std::make_shared<FloatData>(std::round(v->data()));
    } else if (val->type() == Type::Double()) {
        auto v = val->as<DoubleData>(Type::Double());
        return std::make_shared<DoubleData>(std::round(v->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<round> not supported for type " + val->type()->toString());
        return Data::null();
    }
}

EvalResultCode __ceil__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 1, "ceil operator requires exactly one argument");
    auto val = norm[0];
    if (!val->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<ceil> operator requires a primary type");
        return Data::null();
    }

    if (val->type() == Type::Float()) {
        auto v = val->as<FloatData>(Type::Float());
        return std::make_shared<FloatData>(std::ceil(v->data()));
    } else if (val->type() == Type::Double()) {
        auto v = val->as<DoubleData>(Type::Double());
        return std::make_shared<DoubleData>(std::ceil(v->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<ceil> not supported for type " + val->type()->toString());
        return Data::null();
    }
}

EvalResultCode __floor__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 1, "floor operator requires exactly one argument");
    auto val = norm[0];
    if (!val->type()->primary()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<floor> operator requires a primary type");
        return Data::null();
    }

    if (val->type() == Type::Float()) {
        auto v = val->as<FloatData>(Type::Float());
        return std::make_shared<FloatData>(std::floor(v->data()));
    } else if (val->type() == Type::Double()) {
        auto v = val->as<DoubleData>(Type::Double());
        return std::make_shared<DoubleData>(std::floor(v->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<floor> not supported for type " + val->type()->toString());
        return Data::null();
    }
}

EvalResultCode __bin__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 1, "bin operator requires exactly one argument");
    auto val = norm[0];
    if (val->type() != Type::Int32() && val->type() != Type::Int64()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<bin> operator requires integer type");
        return Data::null();
    }

    int64_t number = 0;
    if (val->type() == Type::Int32()) {
        number = val->as<Int32Data>(Type::Int32())->data();
    } else {
        number = val->as<Int64Data>(Type::Int64())->data();
    }

    std::string result = "0b" + std::bitset<64>(number).to_string();
    // 去除前导零
    result.erase(2, result.find('1') - 2);
    return std::make_shared<StringData>(result);
}

EvalResultCode __oct__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 1, "oct operator requires exactly one argument");
    auto val = norm[0];
    if (val->type() != Type::Int32() && val->type() != Type::Int64()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<oct> operator requires integer type");
        return Data::null();
    }

    std::ostringstream oss;
    oss << "0o" << std::oct;
    if (val->type() == Type::Int32()) {
        oss << val->as<Int32Data>(Type::Int32())->data();
    } else {
        oss << val->as<Int64Data>(Type::Int64())->data();
    }

    return std::make_shared<StringData>(oss.str());
}

EvalResultCode __hex__(GIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    ASSERT(norm.size() == 1, "hex operator requires exactly one argument");
    auto val = norm[0];
    if (val->type() != Type::Int32() && val->type() != Type::Int64()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<hex> operator requires integer type");
        return Data::null();
    }

    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase;
    if (val->type() == Type::Int32()) {
        oss << val->as<Int32Data>(Type::Int32())->data();
    } else {
        oss << val->as<Int64Data>(Type::Int64())->data();
    }

    return std::make_shared<StringData>(oss.str());
}
