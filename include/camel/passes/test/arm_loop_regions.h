/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Created: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Test-only: the commit-time check that a loop's regions are regions of where the loop is
 * (docs/technical/17_graph_rewrite_api_contracts.md, 4.1, and H45). It takes a program whose loop
 * is in a branch arm and gives that loop a second body region in the body of the graph, so that
 * the body of the loop is no longer a region of the arm; the commit must refuse it, and the pass
 * prints the message. A camel source program cannot build this graph, so the test of the check is
 * a pass that edits the graph through the draft API.
 */
#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kArmLoopRegionsTestPass = "test::arm_loop_regions";
class ArmLoopRegionsTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};