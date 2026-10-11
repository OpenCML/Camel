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
 * `test::overgrow_graph` commits a graph too large for the runtime representation on purpose:
 * it adds unconnected COPY nodes to `main` until its node blocks exceed what 16 bits address.
 * The passes of the standard pipeline stay below that size (the inliner's owner cap), so this
 * is how the regression tests reach RuntimeDiag::GraphTooLarge of the pass driver.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kTestPassesEnvVar     = "CAMEL_ENABLE_TEST_PASSES";
inline constexpr const char *kDropCallArgumentPass = "test::drop_call_argument";
inline constexpr const char *kOvergrowGraphPass    = "test::overgrow_graph";

class DropCallArgumentTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

class OvergrowGraphTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};
