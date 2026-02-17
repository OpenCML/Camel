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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "math.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/operator.h"

#include <bitset>
#include <cmath> // For std::exp, std::fabs
#include <sstream>

namespace GIR = GraphIR;

slot_t __abs__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    switch (tp) {
    case TypeCode::Int32: {
        Int32 v = norm.get<Int32>(0);
        return toSlot(std::abs(v));
    }
    case TypeCode::Int64: {
        Int64 v = norm.get<Int64>(0);
        return toSlot(std::abs(v));
    }
    case TypeCode::Float32: {
        Float32 v = norm.get<Float32>(0);
        return toSlot(std::fabs(v));
    }
    case TypeCode::Float64: {
        Float64 v = norm.get<Float64>(0);
        return toSlot(std::fabs(v));
    }
    default: {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<abs> not supported for type " + typeCodeToString(tp));
        return NullSlot;
    }
    }
}

slot_t __exp__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    switch (tp) {
    case TypeCode::Float32:
        return toSlot(std::exp(norm.get<Float32>(0)));

    case TypeCode::Float64:
        return toSlot(std::exp(norm.get<Float64>(0)));

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<exp> only supports Float and Double");
        return NullSlot;
    }
}

slot_t __round__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    switch (tp) {
    case TypeCode::Float32:
        return toSlot(std::round(norm.get<Float32>(0)));

    case TypeCode::Float64:
        return toSlot(std::round(norm.get<Float64>(0)));

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<round> only supports Float and Double");
        return NullSlot;
    }
}

slot_t __ceil__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    switch (tp) {
    case TypeCode::Float32:
        return toSlot(std::ceil(norm.get<Float32>(0)));

    case TypeCode::Float64:
        return toSlot(std::ceil(norm.get<Float64>(0)));

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<ceil> only supports Float and Double");
        return NullSlot;
    }
}

slot_t __floor__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    switch (tp) {
    case TypeCode::Float32:
        return toSlot(std::floor(norm.get<Float32>(0)));

    case TypeCode::Float64:
        return toSlot(std::floor(norm.get<Float64>(0)));

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<floor> only supports Float and Double");
        return NullSlot;
    }
}

slot_t __bin__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp    = norm.code(0);
    int64_t number = 0;

    switch (tp) {
    case TypeCode::Int32:
        number = static_cast<int64_t>(norm.get<Int32>(0));
        break;
    case TypeCode::Int64:
        number = static_cast<int64_t>(norm.get<Int64>(0));
        break;
    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<bin> operator requires integer type");
        return NullSlot;
    }

    std::string bits = std::bitset<64>(number).to_string();
    // 去掉前导零，只保留一个 '0b' 前缀
    auto first_one     = bits.find('1');
    std::string body   = (first_one == std::string::npos) ? "0" : bits.substr(first_one);
    std::string result = "0b" + body;

    String *str = String::from(result, mm::autoSpace());
    return toSlot(str);
}

slot_t __oct__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    if (tp != TypeCode::Int32 && tp != TypeCode::Int64) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<oct> operator requires integer type");
        return NullSlot;
    }

    std::ostringstream oss;
    oss << "0o" << std::oct;
    if (tp == TypeCode::Int32)
        oss << norm.get<Int32>(0);
    else
        oss << norm.get<Int64>(0);

    String *result = String::from(oss.str(), mm::autoSpace());
    return toSlot(result);
}

slot_t __hex__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    if (tp != TypeCode::Int32 && tp != TypeCode::Int64) {
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<hex> operator requires integer type");
        return NullSlot;
    }

    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase;
    if (tp == TypeCode::Int32)
        oss << norm.get<Int32>(0);
    else
        oss << norm.get<Int64>(0);

    String *result = String::from(oss.str(), mm::autoSpace());
    return toSlot(result);
}

slot_t __sqrt__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    switch (tp) {
    case TypeCode::Int32:
        return toSlot(std::sqrt(static_cast<Float32>(norm.get<Int32>(0))));

    case TypeCode::Float32:
        return toSlot(std::sqrt(norm.get<Float32>(0)));

    case TypeCode::Int64:
        return toSlot(std::sqrt(static_cast<Float64>(norm.get<Int64>(0))));

    case TypeCode::Float64:
        return toSlot(std::sqrt(norm.get<Float64>(0)));

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sqrt> only supports Float and Double");
        return NullSlot;
    }
}

slot_t __sin__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    switch (tp) {
    case TypeCode::Float32:
        return toSlot(std::sin(norm.get<Float32>(0)));

    case TypeCode::Float64:
        return toSlot(std::sin(norm.get<Float64>(0)));

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<sin> only supports Float and Double");
        return NullSlot;
    }
}

slot_t __cos__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    switch (tp) {
    case TypeCode::Float32:
        return toSlot(std::cos(norm.get<Float32>(0)));

    case TypeCode::Float64:
        return toSlot(std::cos(norm.get<Float64>(0)));

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<cos> only supports Float and Double");
        return NullSlot;
    }
}

slot_t __tan__(ArgsView &with, ArgsView &norm, Context &ctx) {
    TypeCode tp = norm.code(0);

    switch (tp) {
    case TypeCode::Float32:
        return toSlot(std::tan(norm.get<Float32>(0)));

    case TypeCode::Float64:
        return toSlot(std::tan(norm.get<Float64>(0)));

    default:
        ctx.rtmDiags()
            ->of(RuntimeDiag::RuntimeError)
            .commit("<tan> only supports Float and Double");
        return NullSlot;
    }
}
