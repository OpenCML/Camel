/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 25, 2025
 * Updated: May. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * std::macro pass orchestration.
 *
 * Macro execution is a runtime-native rewrite transaction: candidate scan and
 * evaluation read the current immutable closure, rewrite only mutates drafts,
 * and commit atomically publishes a rebuilt reachable runtime closure.
 */

#include "macro.h"
#include "runtime.h"

camel::runtime::GCGraph *MacroRewritePass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    if (!graph) {
        return nullptr;
    }

    camel::runtime::RuntimeGraphDraftSession runtimeSession(context_, graph);
    camel::execute::macro_runtime::MacroEvaluator evaluator(context_);

    while (true) {
        bool roundChanged     = false;
        auto runtimeGraphs    = runtimeSession.collectReachableRuntimeGraphs();
        const auto candidates = camel::execute::macro_runtime::collectMacroCandidates(
            std::span<camel::runtime::GCGraph *const>(runtimeGraphs.data(), runtimeGraphs.size()));

        for (const auto &candidate : candidates) {
            auto result = evaluator.tryEvaluate(candidate, os);
            if (!result.has_value()) {
                continue;
            }
            roundChanged |= camel::execute::macro_runtime::applyMacroRewrite(
                runtimeSession,
                candidate,
                *result,
                os);
        }

        if (!roundChanged) {
            break;
        }
        graph = runtimeSession.commit();
        if (!graph) {
            return nullptr;
        }
        runtimeSession = camel::runtime::RuntimeGraphDraftSession(context_, graph);
    }

    return graph;
}
