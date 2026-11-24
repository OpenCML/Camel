// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* `test::draft_node_owner`: what a node handle answers once the builder that made it is gone and a
 * later builder lands on the address. The reading is in the pass, the assertions are in
 * `test/plans/feat/runtime/draft_node_owner.plan.toml`. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kDraftNodeOwnerTestPass = "test::draft_node_owner";
class DraftNodeOwnerTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};