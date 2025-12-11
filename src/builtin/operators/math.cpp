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
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "math.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"

#include <cmath> // For std::exp, std::fabs

namespace GIR = GraphIR;

void __abs__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    switch (tp) {
    case TypeCode::Int: {
        Int v = frame.get<Int>(nargs[0]);
        frame.set(self, std::abs(v));
        break;
    }
    case TypeCode::Long: {
        Long v = frame.get<Long>(nargs[0]);
        frame.set(self, std::abs(v));
        break;
    }
    case TypeCode::Float: {
        Float v = frame.get<Float>(nargs[0]);
        frame.set(self, std::fabs(v));
        break;
    }
    case TypeCode::Double: {
        Double v = frame.get<Double>(nargs[0]);
        frame.set(self, std::fabs(v));
        break;
    }
    default: {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<abs> not supported for type " + typeCodeToString(tp));
        frame.set(self, NullSlot);
        break;
    }
    }
}

void __exp__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    switch (tp) {
    case TypeCode::Float:
        frame.set(self, std::exp(frame.get<Float>(nargs[0])));
        break;

    case TypeCode::Double:
        frame.set(self, std::exp(frame.get<Double>(nargs[0])));
        break;

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<exp> only supports Float and Double");
        frame.set(self, NullSlot);
    }
}

void __round__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    switch (tp) {
    case TypeCode::Float:
        frame.set(self, std::round(frame.get<Float>(nargs[0])));
        break;

    case TypeCode::Double:
        frame.set(self, std::round(frame.get<Double>(nargs[0])));
        break;

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<round> only supports Float and Double");
        frame.set(self, NullSlot);
    }
}

void __ceil__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    switch (tp) {
    case TypeCode::Float:
        frame.set(self, std::ceil(frame.get<Float>(nargs[0])));
        break;

    case TypeCode::Double:
        frame.set(self, std::ceil(frame.get<Double>(nargs[0])));
        break;

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<ceil> only supports Float and Double");
        frame.set(self, NullSlot);
    }
}

void __floor__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    switch (tp) {
    case TypeCode::Float:
        frame.set(self, std::floor(frame.get<Float>(nargs[0])));
        break;

    case TypeCode::Double:
        frame.set(self, std::floor(frame.get<Double>(nargs[0])));
        break;

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<floor> only supports Float and Double");
        frame.set(self, NullSlot);
    }
}

void __bin__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp    = frame.typeAt(nargs[0]);
    int64_t number = 0;

    switch (tp) {
    case TypeCode::Int:
        number = static_cast<int64_t>(frame.get<Int>(nargs[0]));
        break;
    case TypeCode::Long:
        number = static_cast<int64_t>(frame.get<Long>(nargs[0]));
        break;
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<bin> operator requires integer type");
        frame.set(self, NullSlot);
        return;
    }

    std::string bits = std::bitset<64>(number).to_string();
    // 去掉前导零，只保留一个 '0b' 前缀
    auto first_one     = bits.find('1');
    std::string body   = (first_one == std::string::npos) ? "0" : bits.substr(first_one);
    std::string result = "0b" + body;

    String *str = String::from(result, mm::autoSpace());
    frame.set(self, str);
}

void __oct__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    if (tp != TypeCode::Int && tp != TypeCode::Long) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<oct> operator requires integer type");
        frame.set(self, NullSlot);
        return;
    }

    std::ostringstream oss;
    oss << "0o" << std::oct;
    if (tp == TypeCode::Int)
        oss << frame.get<Int>(nargs[0]);
    else
        oss << frame.get<Long>(nargs[0]);

    String *result = String::from(oss.str(), mm::autoSpace());
    frame.set(self, result);
}

void __hex__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    if (tp != TypeCode::Int && tp != TypeCode::Long) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<hex> operator requires integer type");
        frame.set(self, NullSlot);
        return;
    }

    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase;
    if (tp == TypeCode::Int)
        oss << frame.get<Int>(nargs[0]);
    else
        oss << frame.get<Long>(nargs[0]);

    String *result = String::from(oss.str(), mm::autoSpace());
    frame.set(self, result);
}

void __sqrt__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    switch (tp) {
    case TypeCode::Int:
        frame.set(self, std::sqrt(static_cast<Float>(frame.get<Int>(nargs[0]))));
        break;

    case TypeCode::Float:
        frame.set(self, std::sqrt(frame.get<Float>(nargs[0])));
        break;

    case TypeCode::Long:
        frame.set(self, std::sqrt(static_cast<Float>(frame.get<Long>(nargs[0]))));
        break;

    case TypeCode::Double:
        frame.set(self, std::sqrt(frame.get<Double>(nargs[0])));
        break;

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sqrt> only supports Float and Double");
        frame.set(self, NullSlot);
    }
}

void __sin__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    switch (tp) {
    case TypeCode::Float:
        frame.set(self, std::sin(frame.get<Float>(nargs[0])));
        break;

    case TypeCode::Double:
        frame.set(self, std::sin(frame.get<Double>(nargs[0])));
        break;

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sin> only supports Float and Double");
        frame.set(self, NullSlot);
    }
}

void __cos__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    switch (tp) {
    case TypeCode::Float:
        frame.set(self, std::cos(frame.get<Float>(nargs[0])));
        break;

    case TypeCode::Double:
        frame.set(self, std::cos(frame.get<Double>(nargs[0])));
        break;

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<cos> only supports Float and Double");
        frame.set(self, NullSlot);
    }
}

void __tan__(GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t, Frame &frame, Context &ctx) {
    TypeCode tp = frame.typeAt(nargs[0]);

    switch (tp) {
    case TypeCode::Float:
        frame.set(self, std::tan(frame.get<Float>(nargs[0])));
        break;

    case TypeCode::Double:
        frame.set(self, std::tan(frame.get<Double>(nargs[0])));
        break;

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<tan> only supports Float and Double");
        frame.set(self, NullSlot);
    }
}
