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
 * Created: Sep. 30, 2026
 * Updated: Sep. 30, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Node role registry and the runtime's own roles (see node_roles.h).
 */

#include "camel/runtime/node_roles.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/base.h"

#include <algorithm>
#include <mutex>

namespace camel::runtime {

NodeRoleRegistry &NodeRoleRegistry::instance() {
    static NodeRoleRegistry registry;
    return registry;
}

NodeRoleRegistry::NodeRoleRegistry() {
    // An operator that is not a function of its arguments: it cannot move, merge or vanish.
    entries_.push_back(
        {{"effect", "effect (ordered, never folded or removed)", "#f4cccc"},
         [](const GCGraph &graph, gc_node_ref_t ref) -> std::optional<std::string> {
             const GCNode *node = graph.node(ref);
             if (node->kind != GCNodeKind::Oper) {
                 return std::nullopt;
             }
             const std::string uri(graph.nodeBodyAs<GCOperBody>(ref)->uri());
             if (camel::core::OperatorTraitsRegistry::instance().isPure(uri)) {
                 return std::nullopt;
             }
             return uri;
         }});
    // Where branch arms meet: every arm must produce the type the JOIN carries.
    entries_.push_back(
        {{"branch-contract", "branch contract (arms agree on the result type)", "#d9d2e9"},
         [](const GCGraph &graph, gc_node_ref_t ref) -> std::optional<std::string> {
             const GCNode *node = graph.node(ref);
             if (node->kind != GCNodeKind::Join) {
                 return std::nullopt;
             }
             return node->dataType ? "arms: " + node->dataType->toString() : "arms";
         }});
}

void NodeRoleRegistry::add(NodeRoleSpec spec, NodeRoleClassifier classify) {
    std::unique_lock lock(mutex_);
    auto it = std::find_if(entries_.begin(), entries_.end(), [&](const Entry &e) {
        return e.spec.name == spec.name;
    });
    if (it != entries_.end()) {
        *it = {std::move(spec), std::move(classify)};
        return;
    }
    entries_.push_back({std::move(spec), std::move(classify)});
}

std::vector<NodeRoleRegistry::Match>
NodeRoleRegistry::classify(const GCGraph &graph, gc_node_ref_t ref) const {
    std::shared_lock lock(mutex_);
    std::vector<Match> matches;
    for (const Entry &e : entries_) {
        if (auto detail = e.classify(graph, ref)) {
            matches.push_back({e.spec, std::move(*detail)});
        }
    }
    return matches;
}

} // namespace camel::runtime
