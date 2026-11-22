// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* `test::edit_record`: what the edit record says about one edit of each entry. A draft records the
 * kind of every edit it takes and the regions the edit changed, tells every `EditListener` once per
 * edit, and the publish tier is read off that record (`GraphDraft::editTier`). The reading is in
 * the pass, the assertions are in `test/plans/feat/runtime/edit_record.plan.toml`. */

#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kEditRecordTestPass = "test::edit_record";
class EditRecordTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};