// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The tensor module's fusion groups (see fusion_group.cpp and camel/execute/fusion_group.h).
 */

#pragma once

namespace camel::tensor {

/// Registers the kernel of the `tensor.fusion = "vjp_div_neg"` group: the divide, multiply and
/// neg a division's VJP emits, run in one pass over the buffers. Called once, when the module
/// loads, before any graph runs.
void registerDivideMulNegFusion();

/// Registers the kernel of the `tensor.fusion = "vjp_mul_neg"` group: the multiply and neg of
/// that VJP, run in one pass at the neg off the divide's already-stored value. Called once, when
/// the module loads, before any graph runs.
void registerMulNegFusion();

/// Registers the kernel of the `tensor.fusion = "linear"` group: the product and the bias add of
/// `x @ w + b`, run as the affine transform's one pass, which stores no product. `tensor::fuse`
/// marks the pair (`passes/fuse.cpp#markGroups`); this is the kernel that runs it. Called once,
/// when the module loads, before any graph runs.
void registerLinearFusion();

/// Registers the kernel of the `tensor.fusion = "linear_relu"` group: the same pair with the
/// activation's own kernel at its tail. Called once, when the module loads, before any graph
/// runs.
void registerLinearReluFusion();

} // namespace camel::tensor