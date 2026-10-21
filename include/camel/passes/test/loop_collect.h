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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Test-only producers of the graph-level collect capability (camel/runtime/loop.h, "Collection
 * slots"): no source syntax declares collection slots, so the tests give a loop two of them and
 * observe the tapes and the state afterwards.
 *
 * Both passes rewrite every LOOP node that has no collection slot yet: two slots, the first
 * collecting the next value of the first state, the second the same value plus one (an int
 * state; otherwise the same value twice). The body graph's exit becomes
 * (state…, slot0, slot1) and the LOOP node's value (state…, array0, array1). The loop's own
 * users see that value:
 *   test::loop_collect          the value itself: the program prints (state, tape, tape)
 *   test::loop_collect_summary  (state, len(tape0), len(tape1)), so a loop of a hundred thousand
 *                               rounds prints three numbers instead of the tapes
 */
#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kLoopCollectTestPass        = "test::loop_collect";
inline constexpr const char *kLoopCollectSummaryTestPass = "test::loop_collect_summary";

class LoopCollectTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};

class LoopCollectSummaryTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};