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
             const auto &table = camel::core::OperatorTable::instance();
             if (table.isPure(body->oper)) {
                 return std::nullopt;
             }
             // A higher-order operator is no effect of its own: what it touches is the function
// the program hands it (design principles 3.3), read through
             // `functionValueClause` the way a call through a value is. A type that writes no
             // clause keeps the conservative answer: ordered, never folded away.
             if (const auto *facts = table.semantics(body->oper);
                 facts != nullptr && facts->resources && facts->resources->functionOperand) {
                 const auto &where = *facts->resources->functionOperand;
                 if (where.kind == core::sem::ResourceBinding::Kind::Norm ||
                     where.kind == core::sem::ResourceBinding::Kind::With) {
                     const auto operands = where.kind == core::sem::ResourceBinding::Kind::Norm
                                               ? graph.normInputsOf(ref)
                                               : graph.withInputsOf(ref);
                     const core::type::FunctionType *value = nullptr;
                     if (where.index < operands.size()) {
                         const GCNode *operand = graph.node(operands[where.index]);
                         if (operand != nullptr && operand->dataType() != nullptr &&
                             operand->dataType()->code() == core::type::TypeCode::Function) {
                             value = static_cast<const core::type::FunctionType *>(
                                 operand->dataType());
                         }
                     }
                     if (const auto clause = core::functionValueClause(value)) {
                         if (clause->pure()) {
                             return std::nullopt;
                         }
                         return std::string(body->uri());
                     }
                 }
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
