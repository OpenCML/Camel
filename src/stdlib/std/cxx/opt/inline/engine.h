// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Call-rewrite engine shared by the standalone std::cxx::{inline,devirtualize,specialize}
 * stages and the integrated std::opt driver. The driver adds fold, cse and dce rounds;
 * this engine only performs the call rewrites selected by its configuration.
 */

#pragma once

#include <memory>

#include "camel/execute/pass/opt.h"
#include "config.h"

/// Runs the engine once over `program`, inlining, specializing and devirtualizing by `config`.
///
/// `inlineReport`, when given, receives the record this run made: the direct calls it examined and
/// left, each with the reason it was left and the budget it was measured against. It is the run's
/// own answer and not a value read back off the context, so a caller that has to explain a round
/// which changed nothing -- `std::opt`, which says the growth budget is reached rather than that
/// the optimizer reached a fixed point -- asks the run it just made, and a run with no context to
/// keep a fact on answers all the same. The same record is published on the context as
/// kInlineReportFactKey for the reader that asks long after the run (onnx::export_model explains a
/// call still in the entry graph). One record with two readers, not two copies of one answer.
const camel::runtime::Program *applyOptimizeRewritePass(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    const OptimizeRewriteConfig &config, bool *changed = nullptr,
    std::shared_ptr<const InlineReport> *inlineReport = nullptr);