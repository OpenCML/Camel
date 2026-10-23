// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "operators.h"
#include "half.h"
#include "minmax.h"
#include "numeric.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/half.h"
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

// ---- min, max and abs over bf16 and f16 ------------------------------------------------------
//
// The ruling docs/handover/reviews/reduced-precision.md asks for these three (item 2: "`math::min`
// /`max`/`abs` 等补 bf16/f16"), and design principles 7.3 states how a scalar operation of one of
// the two types is computed: in f32, on operands widened to f32, rounded once into the element
// type — the same one arithmetic path the tensor kernels use (modules/tensor/dtype.h).
//
// For these three the widening is exact and the rounding back is the identity, so what they are is
// the exact operation on the widened value, and that is the reading implemented here:
//
//   * every bf16 and every f16 is an f32 value exactly (include/camel/core/rtdata/half.h), so
//     reading an operand into the f32 the comparison is made in loses nothing, and no comparison
//     is ever made between two 16-bit patterns;
//   * IEEE 754-2019 minimum and maximum return one of their two operands (or the canonical quiet
//     NaN) and every operand is an f32 value the element type holds exactly, so the one store
//     rounding of the result returns that operand bit for bit — nothing is re-computed or
//     re-rounded, and the fold over an array carries an element of T, not an accumulator;
//   * `abs` clears the sign bit: exact for both types, and the only one of the three whose result
//     can differ from an operand's own pattern (-0 becomes +0).
//
// So `min`, `max` and `abs` are per-element operations, not accumulations: no accumulator type and
// no accumulation width take part, and the f64 accumulation rule of design principles 7.3 is about
// a reduction (a sum), not about combining two elements with an operation that never rounds.
//
// The equality case is settled by the sign of zero as it is for f32: -0 is below +0 (design
// principles 7.7), so `min(-0.0, 0.0)` is -0 and `max(-0.0, 0.0)` is +0. `minmax.h` is that
// comparison, shared with the f32 and f64 overloads of numeric.cpp, so it is stated once.

namespace {

using camel::core::math::maxOf;
using camel::core::math::minOf;

template <typename Half> float halfOperand(const slot_t &slot) { return halfToFloat<Half>(slot); }

template <typename Half> slot_t halfMinOrMax(ArgsView &norm, bool minimum) {
    const float a = halfOperand<Half>(norm.slot(0));
    const float b = halfOperand<Half>(norm.slot(1));
    return halfFromFloat<Half>(minimum ? minOf(a, b) : maxOf(a, b));
}

template <typename Half> slot_t halfAbs(ArgsView &norm) {
    return halfFromFloat<Half>(std::fabs(halfOperand<Half>(norm.slot(0))));
}

} // namespace

slot_t __math_min_bf__(ArgsView &, ArgsView &norm, Context &) {
    return halfMinOrMax<BFloat16>(norm, true);
}

slot_t __math_max_bf__(ArgsView &, ArgsView &norm, Context &) {
    return halfMinOrMax<BFloat16>(norm, false);
}

slot_t __math_min_f6__(ArgsView &, ArgsView &norm, Context &) {
    return halfMinOrMax<Float16>(norm, true);
}

slot_t __math_max_f6__(ArgsView &, ArgsView &norm, Context &) {
    return halfMinOrMax<Float16>(norm, false);
}

slot_t __math_abs_bf__(ArgsView &, ArgsView &norm, Context &) { return halfAbs<BFloat16>(norm); }

slot_t __math_abs_f6__(ArgsView &, ArgsView &norm, Context &) { return halfAbs<Float16>(norm); }

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
        // `abs` over the two reduced-precision floating types (ruling item 2): the sign cleared
        // on the value the operand widens to, stored back once (an exact operation; numeric.cpp).
        {"abs_bf", __math_abs_bf__}, {"abs_f6", __math_abs_f6__},
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
