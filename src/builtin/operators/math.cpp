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
 * Updated: Oct. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "math.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

#include <cmath> // For std::exp, std::fabs

namespace GIR = GraphIR;

void __abs__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &val = frame.get(nargs[0]);

    data_ptr_t result;

    if (val->type() == Type::Int()) {
        auto v = val->as<IntData>(Type::Int());
        result = std::make_shared<IntData>(std::abs(v->data()));
    } else if (val->type() == Type::Long()) {
        auto v = val->as<LongData>(Type::Long());
        result = std::make_shared<LongData>(std::abs(v->data()));
    } else if (val->type() == Type::Float()) {
        auto v = val->as<FloatData>(Type::Float());
        result = std::make_shared<FloatData>(std::fabs(v->data()));
    } else if (val->type() == Type::Double()) {
        auto v = val->as<DoubleData>(Type::Double());
        result = std::make_shared<DoubleData>(std::fabs(v->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<abs> not supported for type " + val->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __exp__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &x = frame.get(nargs[0]);

    data_ptr_t result;

    if (x->type() == Type::Float()) {
        auto val = x->as<FloatData>(Type::Float());
        result = std::make_shared<FloatData>(std::exp(val->data()));
    } else if (x->type() == Type::Double()) {
        auto val = x->as<DoubleData>(Type::Double());
        result = std::make_shared<DoubleData>(std::exp(val->data()));
    } else if (x->type() == Type::Int()) {
        auto val = x->as<IntData>(Type::Int());
        result = std::make_shared<DoubleData>(std::exp(static_cast<double>(val->data())));
    } else if (x->type() == Type::Long()) {
        auto val = x->as<LongData>(Type::Long());
        result = std::make_shared<DoubleData>(std::exp(static_cast<double>(val->data())));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<exp> operator not supported for type " + x->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __round__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {

    const data_ptr_t &val = frame.get(nargs[0]);

    data_ptr_t result;

    if (val->type() == Type::Float()) {
        auto v = val->as<FloatData>(Type::Float());
        result = std::make_shared<FloatData>(std::round(v->data()));
    } else if (val->type() == Type::Double()) {
        auto v = val->as<DoubleData>(Type::Double());
        result = std::make_shared<DoubleData>(std::round(v->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<round> not supported for type " + val->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __ceil__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &val = frame.get(nargs[0]);

    data_ptr_t result;

    if (val->type() == Type::Float()) {
        auto v = val->as<FloatData>(Type::Float());
        result = std::make_shared<FloatData>(std::ceil(v->data()));
    } else if (val->type() == Type::Double()) {
        auto v = val->as<DoubleData>(Type::Double());
        result = std::make_shared<DoubleData>(std::ceil(v->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<ceil> not supported for type " + val->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __floor__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &val = frame.get(nargs[0]);

    data_ptr_t result;

    if (val->type() == Type::Float()) {
        auto v = val->as<FloatData>(Type::Float());
        result = std::make_shared<FloatData>(std::floor(v->data()));
    } else if (val->type() == Type::Double()) {
        auto v = val->as<DoubleData>(Type::Double());
        result = std::make_shared<DoubleData>(std::floor(v->data()));
    } else {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<floor> not supported for type " + val->type()->toString());
        frame.set(self, Data::null());
        return;
    }

    frame.set(self, result);
}

void __bin__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &val = frame.get(nargs[0]);

    int64_t number = 0;
    if (val->type() == Type::Int()) {
        number = val->as<IntData>(Type::Int())->data();
    } else {
        number = val->as<LongData>(Type::Long())->data();
    }

    std::string result = "0b" + std::bitset<64>(number).to_string();
    result.erase(2, result.find('1') - 2); // remove leading zeros
    frame.set(self, std::make_shared<StringData>(result));
}

void __oct__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &val = frame.get(nargs[0]);

    if (val->type() != Type::Int() && val->type() != Type::Long()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<oct> operator requires integer type");
        frame.set(self, Data::null());
        return;
    }

    std::ostringstream oss;
    oss << "0o" << std::oct;
    if (val->type() == Type::Int()) {
        oss << val->as<IntData>(Type::Int())->data();
    } else {
        oss << val->as<LongData>(Type::Long())->data();
    }

    frame.set(self, std::make_shared<StringData>(oss.str()));
}

void __hex__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &val = frame.get(nargs[0]);

    if (val->type() != Type::Int() && val->type() != Type::Long()) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<hex> operator requires integer type");
        frame.set(self, Data::null());
        return;
    }

    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase;
    if (val->type() == Type::Int()) {
        oss << val->as<IntData>(Type::Int())->data();
    } else {
        oss << val->as<LongData>(Type::Long())->data();
    }

    frame.set(self, std::make_shared<StringData>(oss.str()));
}
