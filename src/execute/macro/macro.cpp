// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * std::macro pass orchestration.
 *
 * Macro execution is a runtime-native rewrite transaction: candidate scan and
 * evaluation read the current immutable closure, rewrite only mutates drafts,
 * and commit atomically publishes a rebuilt reachable runtime closure.
 */

#include "macro.h"
#include "runtime.h"

const camel::runtime::Program *
MacroRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (!program || !program->root()) {
        return nullptr;
    }

    camel::runtime::RuntimeGraphDraftSession runtimeSession(context_, program);

    while (true) {
        bool roundChanged = false;
        {
            // Results belong to this rewrite transaction. Commit clones the static Function
            // values and retires the old graph carriers; retaining an evaluator's old results
            // into the next round would trace Functions that still name those retired graphs.
            camel::execute::macro_runtime::MacroEvaluator evaluator(
                context_,
                runtimeSession.program());
            auto runtimeGraphs    = runtimeSession.collectReachableRuntimeGraphs();
            const auto candidates = camel::execute::macro_runtime::collectMacroCandidates(
                std::span<camel::runtime::GCGraph *const>(
                    runtimeGraphs.data(),
                    runtimeGraphs.size()));

            // Evaluate first, rewrite afterwards. Evaluation reaches safepoints, where the
            // collector may move the values of this round. The evaluator keeps each of them in a
            // root that is updated in place and that a MacroEvalResult reads through; a draft, on
            // the other hand, holds plain copies of slots (the static slots of the graph it was
            // decoded from, and every value materialized into it) that no root covers. So no
            // draft may exist while a candidate is still being evaluated: all rewrites are applied
            // after the last evaluation, and nothing between them and the commit reaches a
            // safepoint. Candidates are calls of the installed graphs, which the rewrites of the
            // same round do not change, so the order does not affect what is evaluated.
            std::vector<std::pair<
                const camel::execute::macro_runtime::MacroCandidate *,
                camel::execute::macro_runtime::MacroEvalResult>>
                evaluated;
            for (const auto &candidate : candidates) {
                auto result = evaluator.tryEvaluate(candidate, os);
                if (result.has_value()) {
                    evaluated.emplace_back(&candidate, *result);
                }
            }
            for (const auto &[candidate, result] : evaluated) {
                roundChanged |= camel::execute::macro_runtime::applyMacroRewrite(
                    runtimeSession,
                    *candidate,
                    result,
                    os);
            }

            if (!roundChanged) {
                // Nothing evaluates any more: the calls that failed in this round are final
                // failures, and each is an error.
                evaluator.reportFailures();
                break;
            }
        }
        program = runtimeSession.commit();
        if (!program) {
            return nullptr;
        }
        runtimeSession = camel::runtime::RuntimeGraphDraftSession(context_, program);
    }

    return program;
}
