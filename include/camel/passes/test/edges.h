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
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/* Test-only check of the edge invariants a commit enforces (cycle, types along edges). */
#pragma once
#include "camel/execute/pass/opt.h"

inline constexpr const char *kEdgesCycleTestPass = "test::edges_cycle";
inline constexpr const char *kEdgesTypeTestPass  = "test::edges_type";
inline constexpr const char *kEdgesOkTestPass    = "test::edges_ok";
inline constexpr const char *kCommitConflictTestPass = "test::commit_conflict";

// Each adds nodes to the root graph, commits, and prints "EDGES accepted" or
// "EDGES refused: <the commit's message>".
class EdgesCycleTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
class EdgesTypeTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
class EdgesOkTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

// Three sessions on one program: the first commits (and so replaces the context's program),
// the second wrote the same graph, the third another one. Prints what conflicts() says of each
// and what commitChecked() does.
class CommitConflictTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};
