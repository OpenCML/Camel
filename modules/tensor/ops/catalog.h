// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The tensor module's operator catalog, one function per operator family.
 * Registration order matters within an export name: overloads are tried in
 * the order they appear here.
 */

#pragma once

#include "op_def.h"

namespace camel::tensor::ops {

std::vector<OpDef> elementwiseOps(); // arithmetic, comparison, unary maps, where, cast
std::vector<OpDef> creationOps();    // new, zeros, ones, full, random, range, eye, seed
std::vector<OpDef> layoutOps();      // shape, reshape, transpose, permute, concat, slice, ...
std::vector<OpDef> reductionOps();   // sum, mean, max, min, argmax, softmax family, layer_norm
std::vector<OpDef> linalgOps();      // matmul, linear
std::vector<OpDef> utilityOps();     // indexing, show, threading controls

/// `load_npy`'s group: one rule per element type a `.npy` file can hold, each taking the element
/// type the call declares (`load_npy::<f32>(p)`) or the one its position expects. The signature
/// is computed rather than declared (`stdlib/signatures/tensor.cml`), so the group is exported
/// from `TensorModule` by hand, like `from_bytes`.
oper_group_ptr_t loadNpyOperatorGroup();
std::vector<OpDef> gradientOps();    // operators derivative rules emit; zeros_like, numel, ...

/// Gives the operators of `defs` that have a declaration their semantics (semantics.cpp).
void attachSemantics(std::vector<OpDef> &defs);

/// Publishes the tangent space of tensors in the core derivative registry.
void registerTensorTangentSpace();

/// Publishes the core's own slice node (`:op/slice_sub`, the one node `t[a:b:s]` and
/// `slice(t, axis, a, b, s)` both build) and its index declaration in the core's operator table:
/// the node reads a tensor's shape, so the declaration is this module's (indexing.cpp).
void registerCoreSliceIndexing();

} // namespace camel::tensor::ops
