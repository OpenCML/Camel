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

} // namespace camel::tensor