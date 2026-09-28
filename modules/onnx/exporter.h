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
 * Export of a Camel function to an ONNX model by partial evaluation.
 *
 * The exporter walks the function's runtime graph (GCGraph) on demand from
 * its return value, with the parameter bound to a symbolic tensor that
 * carries the example input's dtype and shape. Each node evaluates to a
 * constant or a symbolic tensor (value.h):
 *   - nodes whose inputs are all constants run concretely, through the same
 *     kernels the VMs use (weights, hyper-parameters, shape arithmetic and
 *     closures fold away);
 *   - operators with a symbolic input are lowered to ONNX nodes through the
 *     backend's lowering table (lowering.h), with result facts from the
 *     operator's static inference;
 *   - direct (FUNC) and indirect (CALL) calls are inlined, so recursion that
 *     is driven by constants (e.g. a time-step loop) unrolls;
 *   - a branch with a constant condition selects its arm (data-dependent
 *     control flow is rejected with a diagnostic).
 *
 * Only data dependencies are followed: side effects inside the exported
 * function run once at export time, and control-only (SYNC) ordering has no
 * ONNX counterpart.
 */

#pragma once

#include "proto/onnx_writer.h"

#include "camel/core/context/context.h"

class Function;

namespace camel::tensor {
class TensorObject;
}

namespace camel::onnx {

struct ExportOptions {
    int64_t opset          = kDefaultOpset;
    std::string inputName  = "input";
    std::string outputName = "output";
    std::string graphName  = "camel";
    /// Bound on nested calls; recursion driven by a symbolic value never terminates.
    size_t maxCallDepth = 4096;
};

/// Builds the ONNX model of `fn` applied to a tensor like `example`. Throws ExportError.
Model exportFunction(
    core::context::Context &ctx, ::Function *fn, const tensor::TensorObject *example,
    const ExportOptions &options = {});

} // namespace camel::onnx
