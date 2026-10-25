// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The implementations behind half_ops.h: one arithmetic path for both reduced-precision types.
 * Every operation widens its operands to f32, computes there, and rounds the result once into
 * the element type, which is the path the tensor kernels state (modules/tensor/dtype.h).
 */

#include "half_ops.h"

#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/rtdata/tuple.h"

#include <cmath>
#include <functional>

using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;

namespace {

/// The cell a compound assignment writes (`acc += x`), the same one the f32 forms use.
Tuple *variableOf(ArgsView &norm) { return norm.get<Tuple *>(0); }

template <typename Half> float operand(ArgsView &norm, size_t index) {
    return halfToFloat<Half>(norm.slot(index));
}

/// A binary operation: widen both operands, compute in f32, round once into the element type.
template <typename Half> slot_t binary(ArgsView &norm, const std::function<float(float, float)> &op) {
    return halfFromFloat<Half>(op(operand<Half>(norm, 0), operand<Half>(norm, 1)));
}

template <typename Half>
slot_t comparison(ArgsView &norm, const std::function<bool(float, float)> &op) {
    return toSlot(Bool(op(operand<Half>(norm, 0), operand<Half>(norm, 1))));
}

/// `acc op= x`, in the same one arithmetic path: the cell keeps its element type. What the write
/// gives is the version of the variable: the cell itself, holding what was written.
template <typename Half>
slot_t assign(ArgsView &norm, const std::function<float(float, float)> &op) {
    Tuple *cell = variableOf(norm);
    const float lhs = halfToFloat<Half>(cell->get<slot_t>(0));
    const float rhs = operand<Half>(norm, 1);
    const slot_t result = halfFromFloat<Half>(op(lhs, rhs));
    cell->set<slot_t>(0, result);
    return toSlot(cell);
}

template <typename Half> float divide(float lhs, float rhs) {
    if (rhs == 0.0f) {
        // As for f32 (ops.cpp, __builtin__div__): division by zero is a fault.
        throwRuntimeFault(RuntimeDiag::DivisionByZero);
    }
    return lhs / rhs;
}

} // namespace

// ---- bf16 (`bf`) ----------------------------------------------------------------------------

slot_t __builtin__add_bfbf__(ArgsView &, ArgsView &norm, Context &) {
    return binary<BFloat16>(norm, [](float a, float b) { return a + b; });
}
slot_t __builtin__sub_bfbf__(ArgsView &, ArgsView &norm, Context &) {
    return binary<BFloat16>(norm, [](float a, float b) { return a - b; });
}
slot_t __builtin__mul_bfbf__(ArgsView &, ArgsView &norm, Context &) {
    return binary<BFloat16>(norm, [](float a, float b) { return a * b; });
}
slot_t __builtin__div_bfbf__(ArgsView &, ArgsView &norm, Context &) {
    return binary<BFloat16>(norm, [](float a, float b) { return divide<BFloat16>(a, b); });
}
slot_t __builtin__pow_bfbf__(ArgsView &, ArgsView &norm, Context &) {
    return binary<BFloat16>(norm, [](float a, float b) { return std::pow(a, b); });
}
slot_t __builtin__neg_bf__(ArgsView &, ArgsView &norm, Context &) {
    return halfFromFloat<BFloat16>(-operand<BFloat16>(norm, 0));
}

slot_t __builtin__eq_bf__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<BFloat16>(norm, [](float a, float b) { return a == b; });
}
slot_t __builtin__ne_bf__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<BFloat16>(norm, [](float a, float b) { return a != b; });
}
slot_t __builtin__lt_bf__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<BFloat16>(norm, [](float a, float b) { return a < b; });
}
slot_t __builtin__le_bf__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<BFloat16>(norm, [](float a, float b) { return a <= b; });
}
slot_t __builtin__gt_bf__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<BFloat16>(norm, [](float a, float b) { return a > b; });
}
slot_t __builtin__ge_bf__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<BFloat16>(norm, [](float a, float b) { return a >= b; });
}

slot_t __builtin__assn_add_bf__(ArgsView &, ArgsView &norm, Context &) {
    return assign<BFloat16>(norm, [](float a, float b) { return a + b; });
}
slot_t __builtin__assn_sub_bf__(ArgsView &, ArgsView &norm, Context &) {
    return assign<BFloat16>(norm, [](float a, float b) { return a - b; });
}
slot_t __builtin__assn_mul_bf__(ArgsView &, ArgsView &norm, Context &) {
    return assign<BFloat16>(norm, [](float a, float b) { return a * b; });
}
slot_t __builtin__assn_div_bf__(ArgsView &, ArgsView &norm, Context &) {
    return assign<BFloat16>(norm, [](float a, float b) { return divide<BFloat16>(a, b); });
}
slot_t __builtin__assn_pow_bf__(ArgsView &, ArgsView &norm, Context &) {
    return assign<BFloat16>(norm, [](float a, float b) { return std::pow(a, b); });
}

// ---- f16 (`f6`) -----------------------------------------------------------------------------

slot_t __builtin__add_f6f6__(ArgsView &, ArgsView &norm, Context &) {
    return binary<Float16>(norm, [](float a, float b) { return a + b; });
}
slot_t __builtin__sub_f6f6__(ArgsView &, ArgsView &norm, Context &) {
    return binary<Float16>(norm, [](float a, float b) { return a - b; });
}
slot_t __builtin__mul_f6f6__(ArgsView &, ArgsView &norm, Context &) {
    return binary<Float16>(norm, [](float a, float b) { return a * b; });
}
slot_t __builtin__div_f6f6__(ArgsView &, ArgsView &norm, Context &) {
    return binary<Float16>(norm, [](float a, float b) { return divide<Float16>(a, b); });
}
slot_t __builtin__pow_f6f6__(ArgsView &, ArgsView &norm, Context &) {
    return binary<Float16>(norm, [](float a, float b) { return std::pow(a, b); });
}
slot_t __builtin__neg_f6__(ArgsView &, ArgsView &norm, Context &) {
    return halfFromFloat<Float16>(-operand<Float16>(norm, 0));
}

slot_t __builtin__eq_f6__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<Float16>(norm, [](float a, float b) { return a == b; });
}
slot_t __builtin__ne_f6__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<Float16>(norm, [](float a, float b) { return a != b; });
}
slot_t __builtin__lt_f6__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<Float16>(norm, [](float a, float b) { return a < b; });
}
slot_t __builtin__le_f6__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<Float16>(norm, [](float a, float b) { return a <= b; });
}
slot_t __builtin__gt_f6__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<Float16>(norm, [](float a, float b) { return a > b; });
}
slot_t __builtin__ge_f6__(ArgsView &, ArgsView &norm, Context &) {
    return comparison<Float16>(norm, [](float a, float b) { return a >= b; });
}

slot_t __builtin__assn_add_f6__(ArgsView &, ArgsView &norm, Context &) {
    return assign<Float16>(norm, [](float a, float b) { return a + b; });
}
slot_t __builtin__assn_sub_f6__(ArgsView &, ArgsView &norm, Context &) {
    return assign<Float16>(norm, [](float a, float b) { return a - b; });
}
slot_t __builtin__assn_mul_f6__(ArgsView &, ArgsView &norm, Context &) {
    return assign<Float16>(norm, [](float a, float b) { return a * b; });
}
slot_t __builtin__assn_div_f6__(ArgsView &, ArgsView &norm, Context &) {
    return assign<Float16>(norm, [](float a, float b) { return divide<Float16>(a, b); });
}
slot_t __builtin__assn_pow_f6__(ArgsView &, ArgsView &norm, Context &) {
    return assign<Float16>(norm, [](float a, float b) { return std::pow(a, b); });
}