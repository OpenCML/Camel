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

#include <iostream>
#include <span>

namespace {

/// One line per reachable graph, before the round evaluates anything: the shapes a rewrite can take
/// time on, counted by node kind, and how many rewrites this run has applied so far.
///
/// The pass loops until a round changes nothing, and a round that keeps changing something without
/// ever finishing has had no way to say so -- the run simply stopped returning
/// (`test/cases/modules/onnx/export_loop.cml std::macro` after an edge was deleted was reported as
/// "timeout 124, no output"). This line is the reading that answers "where did it get stuck" before
/// the answer is a timeout: the last line printed names the graph, the calls still there, the
/// branches and loops it holds, and how many rewrites the run had already applied -- a number that
/// stops growing is a round that is spinning rather than working.
///
/// It is a reading and not an assertion: nothing here changes what the pass does, and a plan reads
/// it through `expect_stderr_contains` only. The line goes to stderr and not to the pass's `os`,
/// because the programs this pass runs are asserted on their stdout byte for byte
/// (test/plans/feat/compile/collections.plan.toml and its neighbours): a reading that moved a
/// program's output would be a change to every one of those assertions, and a pass may not make
/// one.
void reportGraphs(std::span<camel::runtime::GCGraph *const> graphs, size_t inlined) {
    std::ostream &os = std::cerr;
    size_t index     = 0;
    for (const camel::runtime::GCGraph *graph : graphs) {
        size_t calls = 0;
        size_t brch  = 0;
        size_t loops = 0;
        if (graph != nullptr) {
            for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
                const camel::runtime::GCNode *node = *it;
                if (node == nullptr) {
                    continue;
                }
                switch (node->kind) {
                case camel::runtime::GCNodeKind::Call:
                    ++calls;
                    break;
                case camel::runtime::GCNodeKind::Brch:
                    ++brch;
                    break;
                case camel::runtime::GCNodeKind::Loop:
                    ++loops;
                    break;
                default:
                    break;
                }
            }
        }
        os << "M[ACRO] graph=" << index << " name=" << (graph ? graph->name() : "?")
           << " calls=" << calls << " inlined=" << inlined << " brch=" << brch
           << " loops=" << loops << "\n";
        ++index;
    }
    // A run that hangs must have this on the terminal, not in a buffer: the line is the only thing
    // that would survive the timeout.
    os.flush();
}

} // namespace

const camel::runtime::Program *
MacroRewritePass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (!program || !program->root()) {
        return nullptr;
    }

    camel::runtime::RuntimeGraphDraftSession runtimeSession(context_, program);

    // How many rewrites this run has applied, across rounds: it is the number that says whether the
    // round loop is making progress.
    size_t inlined = 0;

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
            if (report_) {
                reportGraphs(
                    std::span<camel::runtime::GCGraph *const>(
                        runtimeGraphs.data(),
                        runtimeGraphs.size()),
                    inlined);
            }

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
                if (camel::execute::macro_runtime::applyMacroRewrite(
                        runtimeSession,
                        *candidate,
                        result,
                        os)) {
                    roundChanged = true;
                    ++inlined;
                }
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
