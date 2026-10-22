// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Call-rewrite engine shared by the standalone std::opt::{inline,devirtualize,specialize}
 * stages and the integrated std::opt driver. The driver adds fold, cse and dce rounds;
 * this engine only performs the call rewrites selected by its configuration.
 */

#pragma once

#include "camel/execute/pass/opt.h"
#include "config.h"

const camel::runtime::Program *applyOptimizeRewritePass(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    std::ostream &os, const OptimizeRewriteConfig &config, bool *changed = nullptr);
