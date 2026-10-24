// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Elementwise kernels: unary maps, broadcasting binary arithmetic,
 * broadcasting comparisons, select (`where`), and dtype casts.
 *
 * Operands are described by `Operand`, a non-owning view of a typed buffer
 * with a shape. A Camel scalar becomes a rank-0 operand pointing at a local
 * value, so scalar/tensor mixes go through the same broadcasting path as
 * tensor/tensor operations.
 *
 * Broadcasting follows NumPy: shapes are right-aligned and each pair of
 * extents must be equal or contain a 1.
 */

#pragma once

#include "../tensor.h"

#include <span>

namespace camel::tensor::kernels {

/// Non-owning view of a contiguous typed buffer.
struct Operand {
    const void *data;
    type::TypeCode dtype;
    std::span<const int64_t> shape;
    /// True for a Camel scalar: a value, not a tensor. It takes the other operand's element
    /// type when the two meet (design principles 5.1, the literal takes its context), so a
    /// `2.0` in `x * 2.0` never turns an f32 tensor into an f64 one.
    bool scalar = false;

    static Operand of(const Array *tensor) {
        return {tensor->rawData(), tensor->dtype(), tensor->shapeSpan(), false};
    }
};

/// Scalar storage for a rank-0 operand built from a Camel scalar.
struct ScalarOperand {
    union {
        double d;
        float f;
        int32_t i32;
        int64_t i;
        bool_t b;
        /// The two bytes of a bf16 or f16 element, kept as written: the operand is read at the
        /// element type the value has, so the pattern itself has to be behind it.
        uint16_t u16;
    } value;
    type::TypeCode dtype;

    /// Builds the operand from a Camel numeric slot of type `code`, keeping `code` as written.
    static ScalarOperand fromSlot(type::TypeCode code, slot_t slot);
    Operand view() const { return {&value, dtype, {}, true}; }
};

enum class UnaryOp {
    Neg,
    Abs,
    Exp,
    Log,
    Sqrt,
    Rsqrt,
    Sigmoid,
    Tanh,
    Relu,
    Gelu, // tanh approximation
    Erf,
};

enum class BinaryOp {
    Add,
    Sub,
    Mul,
    Div,
    Pow,
    Max,
    Min,
    /// The remainder `%`. The language's `%` is the truncated remainder
    /// (`camel::utils::remainder`, int_div.h), so on a floating element type it is `fmod`: its
    /// result takes the sign of the left operand, with no flooring, the same definition the
    /// integer element types already have. Appended last: the operator name table in
    /// elementwise_ops.cpp is indexed by this enumerator.
    Mod,
};

enum class CompareOp {
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Equal,
    NotEqual,
};

/// True when the unary op always produces a floating element type (transcendentals, sqrt, ...):
/// f32 for an f32 or integer input, f64 for an f64 one.
bool unaryProducesFloat(UnaryOp op);
/// True when the binary op always produces a floating element type (Div, Pow).
bool binaryProducesFloat(BinaryOp op);

/// Result shape of broadcasting `lhs` with `rhs`. Throws std::invalid_argument on conflict.
Shape broadcastShapes(std::span<const int64_t> lhs, std::span<const int64_t> rhs);

Array *unary(UnaryOp op, const Array *input, mm::IAllocator &allocator);

/// dst[i] = op(src[i]) for float data (src may equal dst), split across threads, with the
/// per-ISA loop `unary` itself uses. A kernel that applies a math function inside a larger
/// computation (softmax's exp) calls this so that it gives the bits the operator gives.
void unaryF32(UnaryOp op, const float *src, float *dst, int64_t count);

/// Result dtype is the promotion of both operands (f64 when either tensor operand is f64, f32
/// for an integer operand of Div/Pow). A scalar operand takes the other operand's element type.
Array *binary(BinaryOp op, Operand lhs, Operand rhs, mm::IAllocator &allocator);

/// Result dtype is bool. Operands are compared in their promoted dtype.
Array *compare(CompareOp op, Operand lhs, Operand rhs, mm::IAllocator &allocator);

/// out = cond ? lhs : rhs, broadcasting all three. `cond` is read as bool.
Array *where(Operand cond, Operand lhs, Operand rhs, mm::IAllocator &allocator);

/// Converts to another storage dtype. Returns a copy even when the dtype matches.
Array *cast(const Array *input, type::TypeCode dtype, mm::IAllocator &allocator);

/// Fills a new tensor with `value` converted to `dtype`.
Array *
full(type::TypeCode dtype, std::span<const int64_t> shape, double value, mm::IAllocator &allocator);

} // namespace camel::tensor::kernels
