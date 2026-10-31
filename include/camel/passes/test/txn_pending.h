// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only check of a graph transaction that owns a graph before the graph has a name.
 *
 * `test::txn_pending` opens a transaction with `openPending`, builds a few nodes and a static
 * value in it, reads the collector's figures, drops the transaction without ever calling
 * `assignId`, and reads the figures again. What the graph made belongs to the transaction
 * (lead ruling of 2026-10-31), so the reading must come back to where it started: the numbers are
 * printed, and the plan asserts the pair rather than a claim that they are "the same".
 */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kTxnPendingTestPass = "test::txn_pending";
class TxnPendingTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};