// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Shared plumbing for operator definitions:
 *   - reading runtime arguments out of an ArgsView (tensors, scalars, shapes);
 *   - `runKernel`, which maps kernel exceptions onto runtime diagnostics;
 *   - static inference helpers that mirror kernel shape rules on possibly
 *     unknown shapes.
 */

#pragma once

#include "../dtype.h"
#include "../kernels/elementwise.h"
#include "camel/core/error/runtime.h"
#include "op_def.h"

namespace camel::tensor::ops {

// ---------------------------------------------------------------- runtime args

/// Tensor argument; numeric arrays are converted when `allowArray` is set.
Array *tensorArg(ArgsView &args, size_t index, bool allowArray = false);

/// Tensor, numeric array, or scalar operand. `storage` keeps a scalar alive for the operand's
/// lifetime.
kernels::Operand operandArg(ArgsView &args, size_t index, kernels::ScalarOperand &storage);

int64_t intArg(ArgsView &args, size_t index);
double numberArg(ArgsView &args, size_t index);
bool boolArg(ArgsView &args, size_t index);
std::string stringArg(ArgsView &args, size_t index);
Shape shapeArg(ArgsView &args, size_t index);

/// True when optional argument `index` was supplied.
inline bool hasArg(ArgsView &args, size_t index) { return index < args.size(); }

slot_t wrap(Array *tensor);

/// The allocator every tensor operator allocates results from.
mm::IAllocator &resultAllocator();

/**
 * Runs a kernel body and translates its exceptions: ShapeError becomes
 * RuntimeDiag::TensorDimensionMismatch, other std::exceptions become
 * RuntimeDiag::RuntimeError. `op` names the operator in the message.
 */
template <typename Fn> slot_t runKernel(std::string_view op, Fn &&body);

[[noreturn]] void reportShapeError(std::string_view op, const char *detail);
[[noreturn]] void reportKernelError(std::string_view op, const char *detail);

template <typename Fn> slot_t runKernel(std::string_view op, Fn &&body) {
    try {
        return body();
    } catch (const ShapeError &e) {
        reportShapeError(op, e.what());
    } catch (const core::error::RuntimeFault &) {
        // A kernel that refuses an argument type raises a runtime fault already naming the
        // operator, the argument and what it takes; it passes through as it was raised.
        throw;
    } catch (const std::exception &e) {
        reportKernelError(op, e.what());
    }
}

// ---------------------------------------------------------------- inference

/// Tensor type from facts.
type::Type *tensorOf(const TensorFacts &facts);
/// "[2, 3]", with "?" for unknown extents, for shape diagnostics.
std::string formatShape(std::span<const int64_t> shape);
type::Type *tensorOf(std::optional<type::TypeCode> dtype, std::optional<StaticShape> shape);

/// Promotion that propagates unknown dtypes.
std::optional<type::TypeCode>
promote(std::optional<type::TypeCode> lhs, std::optional<type::TypeCode> rhs);

/// Broadcast on possibly unknown shapes. Throws ShapeError on a certain conflict.
std::optional<StaticShape>
broadcast(const std::optional<StaticShape> &lhs, const std::optional<StaticShape> &rhs);

/// Shape after reducing `axis` (unknown axis => unknown rank result unless rank known and
/// keepDims).
std::optional<StaticShape>
reduceShape(const std::optional<StaticShape> &input, std::optional<int64_t> axis, bool keepDims);

/// The value of a constant argument (slot of type `type`) as seen by inference: integers,
/// numbers, bools, strings, and int arrays; nullopt for anything else.
std::optional<ConstArg> constArgOf(slot_t slot, type::Type *type);

/// The elements known of an int array written in place (`[n, 3]`), from what the compiler
/// knows about the argument; nullopt when it is not such an array.
std::optional<PartialInts> partialIntsOf(const type::StaticArg &arg, type::Type *type);

/// The constant arguments and the partly known int arrays of a call, for an InferContext.
struct StaticFacts {
    std::vector<std::optional<ConstArg>> constants;
    std::vector<std::optional<PartialInts>> partials;
    StaticFacts(std::span<type::Type *const> types, type::static_args_t statics);
};

/// True when at least one of the first `count` arguments is a tensor.
bool anyTensor(const InferContext &ctx, size_t count);
/// True when at least one of the first `count` arguments is a tensor or an array.
bool anyTensorLike(const InferContext &ctx, size_t count);

} // namespace camel::tensor::ops
