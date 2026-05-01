/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Apr. 12, 2026
 * Updated: Apr. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Compile-time reachable-graph traversal based directly on DraftGraphBuilder.
 */

#include "camel/compile/gir/reachable.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/compile/gir/static_function.h"
#include "camel/core/rtdata/conv.h"
#include "camel/core/type/base.h"

#include <unordered_set>

namespace camel::compile::gir {

void forEachReachableGraph(
    const graph_ptr_t &root, const std::function<void(const graph_ptr_t &)> &visitor) {
    std::unordered_set<DraftGraphBuilder *> visited;

    std::function<void(const graph_ptr_t &)> visit = [&](const graph_ptr_t &curr) {
        if (!curr || !visited.insert(curr.get()).second) {
            return;
        }

        visitor(curr);

        for (const auto &subGraph : curr->subGraphs()) {
            visit(subGraph);
        }

        for (const auto &dep : curr->dependencyGraphs()) {
            visit(dep);
        }

        const auto staticSlots = curr->draft().staticSlots();
        const auto staticTypes = curr->draft().staticSlotTypes();
        for (size_t i = 0; i < staticSlots.size() && i < staticTypes.size(); ++i) {
            auto *type = staticTypes[i];
            if (!type || type->code() != camel::core::type::TypeCode::Function) {
                continue;
            }
            auto *funcObj = camel::core::rtdata::fromSlot<StaticFunction *>(staticSlots[i]);
            if (funcObj && funcObj->graph()) {
                visit(funcObj->graph());
            }
        }

        const auto &draft = curr->draft();
        for (draft_node_ref_t nodeId = 0; nodeId < draft.nodeSlotCount(); ++nodeId) {
            const auto *header = draft.header(nodeId);
            if (!header || header->kind != runtime::GCNodeKind::Func) {
                continue;
            }
            visit(curr->funcTarget(nodeId));
        }
    };

    visit(root);
}

std::vector<graph_ptr_t> collectReachableGraphs(const graph_ptr_t &root) {
    std::vector<graph_ptr_t> graphs;
    forEachReachableGraph(root, [&](const graph_ptr_t &graph) { graphs.push_back(graph); });
    return graphs;
}

} // namespace camel::compile::gir
