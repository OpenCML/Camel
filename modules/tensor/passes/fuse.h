// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * tensor::fuse records implementation choices on the nodes of the program's graphs; it never
 * changes an operator (design principles 9.6).
 *
 *   tensor.kernel  on every tensor:matmul and tensor:linear: "library" when the node may run on
 *                  the CPU library GEMM, "canonical" otherwise. The rule is the core's one
 *                  decision (7.3): the node's effective numeric permissions, read through
 *                  tag::effectivePermissions, which applies the stage's policy
 *                  (relaxed by default: reassociate allowed) and withholds everything from a
 *                  `#strict` node. `tensor::fuse(safe_fp=true)` uses the strict policy, so the
 *                  safe mode's products run the canonical algorithm (7.4). For `linear` the
 *                  attribute is the choice its VJP carries to the matmul gradients; the forward
 *                  kernel is the canonical x @ w + b.
 *   tensor.fusion  on the nodes of `x @ w + b` and `relu(x @ w + b)` when nothing outside the
 *                  pattern observes the product: "linear" or "linear_relu" on the matmul, the
 *                  add and the relu, so a backend can run the group as one kernel while the
 *                  graph keeps the program's operators.
 *
 * The matmul kernel reads tensor.kernel through ArgsView::attrs (the scheduler hands the node's
 * attribute set to the kernel). Because the operators stay matmul/add/relu, the ONNX exporter
 * and Tiles see the program's operators, and the group is visible to them as the attribute.
 */

#pragma once

#include "camel/execute/pass/opt.h"

namespace camel::tensor::passes {

class TensorFusePass : public RuntimeGraphRewritePass {
  public:
    TensorFusePass(const camel::core::context::context_ptr_t &ctx, bool safeFp)
        : RuntimeGraphRewritePass(ctx), safeFp_(safeFp) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    /// tensor::fuse(safe_fp=true): the canonical algorithm for every matmul (design principles
    /// 7.4); the default uses the stage's relaxed policy (7.3).
    bool safeFp_ = false;
};

/// Registers the tensor module's passes (tensor::fuse) with the pass registry.
void registerTensorPasses();

} // namespace camel::tensor::passes