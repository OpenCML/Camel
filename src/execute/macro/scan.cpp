// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Macro candidate collection.
 *
 * This layer only records possible macro call sites from immutable runtime
 * graphs. Static-input checks and execution policy are owned by the evaluator.
 */

#include "runtime.h"

namespace camel::execute::macro_runtime {

namespace {

bool isMacroOper(camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t ref) {
    const auto *node = graph ? graph->node(ref) : nullptr;
    return node != nullptr && node->kind == camel::runtime::GCNodeKind::Oper && node->isMacro();
}

} // namespace

std::vector<MacroCandidate>
collectMacroCandidates(std::span<camel::runtime::GCGraph *const> closure) {
    std::vector<MacroCandidate> candidates;
    for (camel::runtime::GCGraph *graph : closure) {
        if (!graph) {
            continue;
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const auto *node = *it;
            if (!node) {
                continue;
            }
            if (node->kind == camel::runtime::GCNodeKind::Func ||
                node->kind == camel::runtime::GCNodeKind::Call || isMacroOper(graph, it.ref())) {
                candidates.push_back(MacroCandidate{.runtimeGraph = graph, .nodeRef = it.ref()});
            }
        }
    }
    return candidates;
}

} // namespace camel::execute::macro_runtime
