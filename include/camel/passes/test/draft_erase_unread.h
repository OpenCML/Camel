// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* `test::draft_erase_unread`: the one guarantee `GraphDraft::eraseUnreadNodes` has over the
 * `eraseNode` call it replaced -- a set that anything outside it still reads is refused whole, and
 * the graph is left exactly as it was found. The reading is in the pass, the assertions are in
 * `test/plans/feat/runtime/draft_erase_unread.plan.toml`. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kDraftEraseUnreadTestPass = "test::draft_erase_unread";
class DraftEraseUnreadTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};