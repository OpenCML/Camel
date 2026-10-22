// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
             const auto *body = graph.nodeBodyAs<GCOperBody>(ref);
             if (camel::core::OperatorTable::instance().isPure(body->oper)) {
                 return std::nullopt;
             }
             return std::string(body->uri());
         }});
    // Where branch arms meet: every arm must produce the type the JOIN carries.
    entries_.push_back(
        {{"branch-contract", "branch contract (arms agree on the result type)", "#d9d2e9"},
         [](const GCGraph &graph, gc_node_ref_t ref) -> std::optional<std::string> {
             const GCNode *node = graph.node(ref);
             if (node->kind != GCNodeKind::Join) {
                 return std::nullopt;
             }
             return node->dataType() ? "arms: " + node->dataType()->toString() : "arms";
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
