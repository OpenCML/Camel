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
 * Created: Oct. 04, 2026
 * Updated: Oct. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * onnx::export: the ONNX exporter as the last pass of a pipeline.
 *
 *   camel m.cml std::macro std::opt::simplify onnx::export
 *
 * The export request is the program's own `onnx.export_model(fn, example,
 * path[, dynamic_axes[, target]])` call: the pass looks for it in the entry
 * graph the preceding passes left, where its arguments must be constants (the
 * function, with constant captures; the example values; the options). The
 * output path may instead come from the environment variable CAMEL_ONNX_OUT,
 * which overrides the call's path argument.
 *
 * The pass translates the function's graph as the preceding passes left it
 * (exporter.h, translateFunction). It does not expand macros, devirtualize,
 * specialize calls, inline or unroll: a function graph that still contains a
 * call or an unexpanded macro operator is refused with the diagnostic
 * RuntimeDiag::TranslationRefused at the node, naming the pass that should
 * have run. The one preparation it performs itself is the one that needs the
 * example inputs, which no earlier pass has: the parameters take the examples'
 * types (dtype and shape), and what those types make constant is folded
 * (std::opt::fold and std::opt::dce on the bound copy of the function; the
 * program's own graphs are not modified).
 *
 * Nothing is written when any request is refused, and the program is not
 * executed (the pass consumes the graph).
 */

#pragma once

#include "camel/execute/pass/trans.h"

namespace camel::onnx::passes {

class OnnxExportPass : public GraphTranslatePass {
  public:
    using GraphTranslatePass::GraphTranslatePass;

    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

/// Registers the onnx module's passes (onnx::export) with the pass registry.
void registerOnnxPasses();

} // namespace camel::onnx::passes
