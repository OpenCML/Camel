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
 * Created: May. 04, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Macro candidate collection.
 *
 * This layer only records possible macro call sites from immutable runtime
 * graphs. Static-input checks and execution policy are owned by the evaluator.
 */

#include "runtime.h"

namespace camel::execute::macro_runtime {

namespace {

bool isNativeMacroOperUri(std::string_view uri) { return uri == "nn:apply_gradients"; }

bool isMacroOper(camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t ref) {
    const auto *node = graph ? graph->node(ref) : nullptr;
    if (!node || node->kind != camel::runtime::GCNodeKind::Oper) {
        return false;
    }
    if (node->isMacro()) {
        return true;
    }
    const auto *body = graph->nodeBodyAs<camel::runtime::GCOperBody>(ref);
    return body != nullptr && isNativeMacroOperUri(body->uri());
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
