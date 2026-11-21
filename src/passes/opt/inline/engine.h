// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Call-rewrite engine shared by the standalone std::cxx::{inline,devirtualize,specialize}
 * stages and the integrated std::opt driver. The driver adds fold, cse and dce rounds;
 * this engine only performs the call rewrites selected by its configuration.
 */

#pragma once

#include <optional>

#include "camel/execute/pass/opt.h"
#include "config.h"

/// Runs the engine once over `program`, inlining, specializing and devirtualizing by `config`.
///
/// `growthBudgetLeft`, when given, receives the direct call this run left because the growth
/// budget is spent -- the one reason whose presence means the run did not finish its work, as
/// against one that stopped because the strategy does not select the call. It is an out
/// parameter and not a pass fact because the answer is this run's own: a caller that has to
/// explain a round that changed nothing asks the run that changed nothing, instead of reading a
/// value a later pass has to keep fresh on the context. (`InlineReport` is the same record on the
/// context, for a pass that has to explain a call long after the run: onnx::export.)
const camel::runtime::Program *applyOptimizeRewritePass(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    std::ostream &os, const OptimizeRewriteConfig &config, bool *changed = nullptr,
    const camel::runtime::GCGraph **lastChangedGraph = nullptr,
    std::optional<InlineDeclined> *growthBudgetLeft  = nullptr);
