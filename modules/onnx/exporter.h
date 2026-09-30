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
 * Export of a Camel function to an ONNX model.
 *
 * Export is translation. The function applied to an input of the example's
 * type (dtype and shape, with ExportOptions::dynamicAxes left unknown) is
 * first simplified by the generic passes (std::opt::simplify) as a program of
 * its own: its captures become constants, shapes are specialized, calls are
 * inlined, recursion of static depth unrolls, and everything that does not
 * depend on the input folds into constants. The exporter then translates the
 * remaining graph node by node:
 *   - operators are lowered through the backend's lowering table
 *     (lowering.h), with result facts from the operator's static inference;
 *   - constants become initializers, named after the struct fields they
 *     were captured in;
 *   - a branch on a condition computed from the input becomes an ONNX If
 *     with one subgraph per arm;
 *   - shape arithmetic on dynamic dimensions becomes Shape/Gather/Concat.
 * What cannot be translated is reported: a call that remains (recursion
 * whose depth depends on the input), an impure operator, an operator without
 * a lowering.
 */

#pragma once

#include "proto/onnx_writer.h"

#include "camel/core/context/context.h"
#include "camel/core/type/base.h"

#include <span>
#include <vector>

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
    /// Axes of the tensor arguments left dynamic in the model (e.g. {0} for a variable batch
    /// size). The examples' extents on these axes are not baked into the graph.
    std::vector<int64_t> dynamicAxes;
};

/// An example argument: a value whose types (dtypes and shapes) the graph inputs take.
struct Example {
    slot_t value;
    core::type::Type *type;
};

/**
 * Builds the ONNX model of `fn` applied to arguments like `examples` (one per parameter).
 * Tensor arguments become graph inputs (`input`, or `input0`, `input1`, ... when there are
 * several); tuple and struct arguments (a model's parameters) become one input per tensor,
 * named by field (`fc1_w`) or path (`block_q_w`). The result becomes one output per tensor or
 * number: `output`, or `output_0`, `output_1_fc1_w`, ... for tuples and structs. Throws
 * ExportError.
 */
Model exportFunction(
    core::context::Context &ctx, ::Function *fn, std::span<const Example> examples,
    const ExportOptions &options = {});

} // namespace camel::onnx
