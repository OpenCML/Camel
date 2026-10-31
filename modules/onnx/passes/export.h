// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * onnx::export: the ONNX exporter as the last pass of a pipeline.
 *
 *   camel m.cml std::macro std::opt onnx::export
 *
 * The export request is the program's own `onnx.export_model(fn, example,
 * path[, dynamic_axes[, target]])` call: the pass looks for it in the entry
 * graph the preceding passes left, where its arguments must be constants (the
 * function, with constant captures; the example values; the options). The
 * output path may instead be given as the pass's parameter `out`,
 * `onnx::export out="model.onnx"`, which overrides the call's path argument.
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
 * What it prints, it prints because a parameter asked for it: `trace=true` every
 * lowered call with the facts of its arguments, `dump=true` the simplified graph
 * that is translated, `prep_stats=true` the size of the function when the export
 * receives it and after its own simplification. A run that asks for nothing
 * prints nothing but the file it wrote.
 *
 * Nothing is written when any request is refused, and the program is not
 * executed (the pass consumes the graph).
 */

#pragma once

#include "camel/execute/pass/trans.h"

namespace camel::onnx::passes {

class OnnxExportPass : public GraphTranslatePass {
  public:
    OnnxExportPass(
        const camel::core::context::context_ptr_t &ctx, std::string out = {}, bool trace = false,
        bool dump = false, bool prepStats = false)
        : GraphTranslatePass(ctx), out_(std::move(out)), trace_(trace), dump_(dump),
          prepStats_(prepStats) {}

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

  private:
    std::string out_; // the parameter `out`; empty: not given
    /// The parameters `trace`, `dump` and `prep_stats`: what the pass prints beyond the file.
    bool trace_     = false;
    bool dump_      = false;
    bool prepStats_ = false;
};

/// Registers the onnx module's passes (onnx::export) with the pass registry.
void registerOnnxPasses();

} // namespace camel::onnx::passes
