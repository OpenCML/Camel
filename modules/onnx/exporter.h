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
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Export of a Camel function to an ONNX model.
 *
 * Export is translation. The function applied to an input of the example's
 * type (dtype and shape, with ExportOptions::dynamicAxes left unknown) is
 * first simplified by the generic passes (std::opt) as a program of
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

namespace camel::runtime {
class GCGraph;
}

namespace camel::tensor {
class TensorObject;
}

namespace camel::onnx {

struct ExportOptions {
    int64_t opset          = kDefaultOpset;
    std::string inputName  = "input";
    std::string outputName = "output";
    std::string graphName  = "camel";
    /// Runtime the model is for ("onnxruntime-cpu"); every node is checked against its
    /// capability data (capability.h). "none" or empty skips the check.
    std::string target = "onnxruntime-cpu";
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

/**
 * The export of the onnx::export pass (passes/export.h): translates the graph of `fn` as the
 * preceding passes left it. `captures` binds the closure slots of a function whose closure the
 * program builds from constants (empty when `fn` carries its own closure or has none).
 *
 * Unlike exportFunction this does not simplify the function: no macro expansion, no
 * devirtualization, no call specialization, no inlining, no unrolling. A call that remains is
 * an ExportError of kind CallRemains. The one thing no earlier pass can do is done here,
 * because it needs the examples: the parameters take the examples' exact types (dtype and
 * shape), node types are re-inferred, and what those types make constant (the shape of an
 * input and the arithmetic on it) is folded and the dead nodes are dropped, i.e. the
 * std::opt::fold / std::opt::dce fixpoint with every call-rewriting phase off.
 */
Model translateFunction(
    core::context::Context &ctx, ::Function *fn, std::span<const Example> captures,
    std::span<const Example> examples, const ExportOptions &options = {});

/**
 * The export of a program that makes no export request: translates the entry graph `root`
 * itself, that is the computation of `main`. Each parameter the entry graph still has becomes
 * model inputs declared from its type (`with` parameters first; a tensor needs its element
 * type and shape, a negative extent is a dynamic axis; a number is a rank-0 input; a struct or
 * tuple gives one input per leaf). Parameters that were given a value from outside
 * (camel/execute/entry_args.h) are constants of the graph by now and become initializers named
 * after the parameter. The result of `main` becomes the outputs as in exportFunction. The
 * graph is simplified like translateFunction simplifies its function; the program itself is
 * left as it is. Throws ExportError.
 */
Model translateEntryGraph(
    core::context::Context &ctx, camel::runtime::GCGraph *root, const ExportOptions &options = {});

} // namespace camel::onnx
