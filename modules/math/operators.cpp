/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/string.h"

#include <bitset>
#include <cmath>
#include <sstream>

slot_t __math_abs__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Int32:
        return toSlot(std::abs(norm.get<Int32>(0)));
    case TypeCode::Int64:
        return toSlot(std::abs(norm.get<Int64>(0)));
    case TypeCode::Float32:
        return toSlot(std::fabs(norm.get<Float32>(0)));
    case TypeCode::Float64:
        return toSlot(std::fabs(norm.get<Float64>(0)));
    default:
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<abs> not supported");
        return NullSlot;
    }
}

slot_t __math_exp__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(std::exp(norm.get<Float32>(0)));
    case TypeCode::Float64:
        return toSlot(std::exp(norm.get<Float64>(0)));
    default:
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<exp> only Float/Double");
        return NullSlot;
    }
}

slot_t __math_round__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(std::round(norm.get<Float32>(0)));
    case TypeCode::Float64:
        return toSlot(std::round(norm.get<Float64>(0)));
    default:
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<round> only Float/Double");
        return NullSlot;
    }
}

slot_t __math_ceil__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(std::ceil(norm.get<Float32>(0)));
    case TypeCode::Float64:
        return toSlot(std::ceil(norm.get<Float64>(0)));
    default:
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<ceil> only Float/Double");
        return NullSlot;
    }
}

slot_t __math_floor__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(std::floor(norm.get<Float32>(0)));
    case TypeCode::Float64:
        return toSlot(std::floor(norm.get<Float64>(0)));
    default:
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<floor> only Float/Double");
        return NullSlot;
    }
}

slot_t __math_bin__(ArgsView &with, ArgsView &norm, Context &ctx) {
    int64_t number = 0;
    switch (norm.code(0)) {
    case TypeCode::Int32:
        number = static_cast<int64_t>(norm.get<Int32>(0));
        break;
    case TypeCode::Int64:
        number = static_cast<int64_t>(norm.get<Int64>(0));
        break;
    default:
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<bin> requires integer");
        return NullSlot;
    }
    std::string bits = std::bitset<64>(number).to_string();
    auto first_one   = bits.find('1');
    std::string body = (first_one == std::string::npos) ? "0" : bits.substr(first_one);
    return toSlot(String::from("0b" + body, mm::autoSpace()));
}

slot_t __math_oct__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.code(0) != TypeCode::Int32 && norm.code(0) != TypeCode::Int64) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<oct> requires integer");
        return NullSlot;
    }
    std::ostringstream oss;
    oss << "0o" << std::oct;
    if (norm.code(0) == TypeCode::Int32)
        oss << norm.get<Int32>(0);
    else
        oss << norm.get<Int64>(0);
    return toSlot(String::from(oss.str(), mm::autoSpace()));
}

slot_t __math_hex__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.code(0) != TypeCode::Int32 && norm.code(0) != TypeCode::Int64) {
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<hex> requires integer");
        return NullSlot;
    }
    std::ostringstream oss;
    oss << "0x" << std::hex << std::uppercase;
    if (norm.code(0) == TypeCode::Int32)
        oss << norm.get<Int32>(0);
    else
        oss << norm.get<Int64>(0);
    return toSlot(String::from(oss.str(), mm::autoSpace()));
}

slot_t __math_sqrt__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Int32:
        return toSlot(std::sqrt(static_cast<Float32>(norm.get<Int32>(0))));
    case TypeCode::Float32:
        return toSlot(std::sqrt(norm.get<Float32>(0)));
    case TypeCode::Int64:
        return toSlot(std::sqrt(static_cast<Float64>(norm.get<Int64>(0))));
    case TypeCode::Float64:
        return toSlot(std::sqrt(norm.get<Float64>(0)));
    default:
        ctx.rtmDiags()->of(RuntimeDiag::RuntimeError).commit("<sqrt> only Float/Double");
        return NullSlot;
    }
}

std::unordered_map<std::string, operator_t> getMathOpsMap() {
    return {
        {"abs_i", __math_abs__},     {"abs_l", __math_abs__},     {"abs_f", __math_abs__},
        {"abs_d", __math_abs__},     {"exp_f", __math_exp__},     {"exp_d", __math_exp__},
        {"round_f", __math_round__}, {"round_d", __math_round__}, {"ceil_f", __math_ceil__},
        {"ceil_d", __math_ceil__},   {"floor_f", __math_floor__}, {"floor_d", __math_floor__},
        {"bin_i", __math_bin__},     {"bin_l", __math_bin__},     {"oct_i", __math_oct__},
        {"oct_l", __math_oct__},     {"hex_i", __math_hex__},     {"hex_l", __math_hex__},
        {"sqrt", __math_sqrt__},
    };
}
