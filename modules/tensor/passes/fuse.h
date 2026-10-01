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
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * tensor::fuse: rewrites `x @ w + b` and `relu(x @ w + b)` into single
 * matmul_add / matmul_add_relu operator nodes.
 *
 * The fused kernels fold the bias into the GEMM and apply relu in place, which
 * removes one (or two) intermediate tensors and passes over memory per linear
 * layer. They keep the pattern's semantics for every argument shape and dtype
 * (falling back to the unfused computation when the bias is not a row bias),
 * so the rewrite needs no static shape information. A pattern is fused only
 * when nothing outside it observes its intermediate results (their control
 * dependencies move to the fused node).
 */

#pragma once

#include "camel/execute/pass/opt.h"

namespace camel::tensor::passes {

class TensorFusePass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;

    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

/// Registers the tensor module's passes (tensor::fuse) with the pass registry.
void registerTensorPasses();

} // namespace camel::tensor::passes
