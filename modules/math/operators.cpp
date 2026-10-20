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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "numeric.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/string.h"

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;

#include <bitset>

using namespace camel::core::error;
#include <cmath>
#include <complex>
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
    case TypeCode::Complex64: {
        // |a + bi| = hypot(a, b), in the width of the parts (f32 for c64).
        const Complex *z = norm.get<Complex *>(0);
        return toSlot(static_cast<Float32>(std::hypot(z->realF(), z->imaginaryF())));
    }
    case TypeCode::Complex128: {
        const Complex *z = norm.get<Complex *>(0);
        return toSlot(std::hypot(z->real(), z->imaginary()));
    }
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "math.abs",
            typeCodeToString(norm.code(0)),
            "a number");
    }
}

slot_t __math_exp__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(std::exp(norm.get<Float32>(0)));
    case TypeCode::Float64:
        return toSlot(std::exp(norm.get<Float64>(0)));
    case TypeCode::Complex64: {
        const Complex *z = norm.get<Complex *>(0);
        const auto r     = std::exp(std::complex<float>{z->realF(), z->imaginaryF()});
        return toSlot(
            Complex::create(r.real(), r.imag(), TypeCode::Complex64, mm::autoSpace()));
    }
    case TypeCode::Complex128: {
        const Complex *z = norm.get<Complex *>(0);
        const auto r     = std::exp(std::complex<double>{z->real(), z->imaginary()});
        return toSlot(
            Complex::create(r.real(), r.imag(), TypeCode::Complex128, mm::autoSpace()));
    }
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "math.exp",
            typeCodeToString(norm.code(0)),
            "a float or a double");
    }
}

slot_t __math_re__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Complex *z = norm.get<Complex *>(0);
    if (norm.code(0) == TypeCode::Complex64) {
        return toSlot(z->realF());
    }
    return toSlot(z->real());
}

slot_t __math_im__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Complex *z = norm.get<Complex *>(0);
    if (norm.code(0) == TypeCode::Complex64) {
        return toSlot(z->imaginaryF());
    }
    return toSlot(z->imaginary());
}

slot_t __math_conj__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Complex *z    = norm.get<Complex *>(0);
    const TypeCode code = norm.code(0);
    if (code == TypeCode::Complex64) {
        return toSlot(Complex::create(
            z->realF(), -z->imaginaryF(), TypeCode::Complex64, mm::autoSpace()));
    }
    return toSlot(
        Complex::create(z->real(), -z->imaginary(), TypeCode::Complex128, mm::autoSpace()));
}

slot_t __math_round__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(std::round(norm.get<Float32>(0)));
    case TypeCode::Float64:
        return toSlot(std::round(norm.get<Float64>(0)));
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "math.round",
            typeCodeToString(norm.code(0)),
            "a float or a double");
    }
}

slot_t __math_ceil__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(std::ceil(norm.get<Float32>(0)));
    case TypeCode::Float64:
        return toSlot(std::ceil(norm.get<Float64>(0)));
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "math.ceil",
            typeCodeToString(norm.code(0)),
            "a float or a double");
    }
}

slot_t __math_floor__(ArgsView &with, ArgsView &norm, Context &ctx) {
    switch (norm.code(0)) {
    case TypeCode::Float32:
        return toSlot(std::floor(norm.get<Float32>(0)));
    case TypeCode::Float64:
        return toSlot(std::floor(norm.get<Float64>(0)));
    default:
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "math.floor",
            typeCodeToString(norm.code(0)),
            "a float or a double");
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
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "math.bin",
            typeCodeToString(norm.code(0)),
            "an integer");
    }
    std::string bits = std::bitset<64>(number).to_string();
    auto first_one   = bits.find('1');
    std::string body = (first_one == std::string::npos) ? "0" : bits.substr(first_one);
    return toSlot(String::from("0b" + body, mm::autoSpace()));
}

slot_t __math_oct__(ArgsView &with, ArgsView &norm, Context &ctx) {
    if (norm.code(0) != TypeCode::Int32 && norm.code(0) != TypeCode::Int64) {
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "math.oct",
            typeCodeToString(norm.code(0)),
            "an integer");
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
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "math.hex",
            typeCodeToString(norm.code(0)),
            "an integer");
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
        throwRuntimeFault(
            RuntimeDiag::OperandTypeUnsupported,
            "math.sqrt",
            typeCodeToString(norm.code(0)),
            "a float or a double");
    }
}

std::unordered_map<std::string, operator_t> getMathOpsMap() {
    std::unordered_map<std::string, operator_t> ops = getMathNumericOpsMap();
    ops.merge(std::unordered_map<std::string, operator_t>{
        {"abs_i", __math_abs__},     {"abs_l", __math_abs__},     {"abs_f", __math_abs__},
        {"abs_d", __math_abs__},     {"abs_c", __math_abs__},     {"abs_z", __math_abs__},
        {"exp_f", __math_exp__},     {"exp_d", __math_exp__},     {"exp_c", __math_exp__},
        {"exp_z", __math_exp__},
        {"round_f", __math_round__}, {"round_d", __math_round__}, {"ceil_f", __math_ceil__},
        {"ceil_d", __math_ceil__},   {"floor_f", __math_floor__}, {"floor_d", __math_floor__},
        {"bin_i", __math_bin__},     {"bin_l", __math_bin__},     {"oct_i", __math_oct__},
        {"oct_l", __math_oct__},     {"hex_i", __math_hex__},     {"hex_l", __math_hex__},
        {"sqrt", __math_sqrt__},
        {"re_c", __math_re__},   {"re_z", __math_re__},
        {"im_c", __math_im__},   {"im_z", __math_im__},
        {"conj_c", __math_conj__}, {"conj_z", __math_conj__},
    });
    return ops;
}
