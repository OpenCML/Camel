// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::tags` reads a declaration fact back from the runtime graph, the way a
 * scheduler does, and prints one line per graph: `TAGS independent <name> yes|no`. It covers
 * the runtime query of the standard tag `independent` (design principles 3.6), which no
 *  scheduler consumes yet (the question is in).
 *
 * Only nameable when CAMEL_ENABLE_TEST_PASSES is set (fault.h). The program is returned
 * unchanged.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kTagsTestPass = "test::tags";

class TagsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};