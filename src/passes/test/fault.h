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
 * Author: Zhenjie Wei
 * Created: Oct. 04, 2026
 * Updated: Oct. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY fault-injection passes. They are not part of any pipeline and cannot be named unless
 * the environment variable CAMEL_ENABLE_TEST_PASSES is set.
 *
 * `test::drop_call_argument` commits a structurally broken program on purpose: it removes the
 * last argument of the first direct call (FUNC node) whose callee takes one. The regression
 * tests use it to check that the structural validation run when a rewrite is committed rejects
 * such a graph in every build mode, as an internal error naming the pass.
 *
 * `test::leak_branch_arm` and `test::share_branch_arm` commit a graph that breaks the branch-arm
 * invariant (docs/technical/17_graph_rewrite_api_contracts.md, 4.1) on purpose, in the first
 * branch of `main`: the first adds a node that waits on the BRCH and that only the JOIN waits
 * on (it belongs to no arm), the second makes the second arm wait on the first (the first arm's
 * tail then belongs to two arms). Both graphs are otherwise well formed; before the invariant
 * was checked at commit, the back ends ran them and computed something else.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kTestPassesEnvVar     = "CAMEL_ENABLE_TEST_PASSES";
inline constexpr const char *kDropCallArgumentPass = "test::drop_call_argument";
inline constexpr const char *kLeakBranchArmPass    = "test::leak_branch_arm";
inline constexpr const char *kShareBranchArmPass   = "test::share_branch_arm";

class DropCallArgumentTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class LeakBranchArmTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class ShareBranchArmTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
