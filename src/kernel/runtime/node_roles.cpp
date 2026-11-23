// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Node role registry and the runtime's own roles (see node_roles.h). Both roles are written over the
 * view, so a role is the same rule for a frozen graph and for a draft.
 */

#include "camel/runtime/node_roles.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/base.h"

#include <algorithm>
#include <mutex>

namespace camel::runtime {
namespace {

using camel::graph::GraphView;
using camel::graph::NodeKind;
using camel::graph::NodeRef;
using camel::graph::PortGroup;

/// The operator a node names, or nullopt when it names none. `NodeKind::Oper` is not enough on its
/// own: the view folds CAST, COPY, FILL and ACCS into that kind, and those four keep their own
/// payload in both representations today, so `operatorOf` answers `kNoOper` for them -- which is what
/// the old reading said too (it asked for a stored OPER). A role that speaks about operators asks
/// this; a role that speaks about a node's shape asks the kind.
std::optional<camel::core::OperId> operatorOf(const GraphView &graph, NodeRef ref) {
    if (graph.kindOf(ref) != NodeKind::Oper) {
        return std::nullopt;
    }
    const camel::core::OperId oper = graph.operatorOf(ref);
    return oper == camel::core::kNoOper ? std::nullopt : std::optional<camel::core::OperId>(oper);
}

} // namespace

NodeRoleRegistry &NodeRoleRegistry::instance() {
    static NodeRoleRegistry registry;
    return registry;
}

NodeRoleRegistry::NodeRoleRegistry() {
    // An operator that is not a function of its arguments: it cannot move, merge or vanish.
    entries_.push_back(
        {{"effect", "effect (ordered, never folded or removed)", "#f4cccc"},
         [](const GraphView &graph, NodeRef ref) -> std::optional<std::string> {
             const std::optional<camel::core::OperId> oper = operatorOf(graph, ref);
             if (!oper.has_value()) {
                 return std::nullopt;
             }
             // The record's answer (`sem::Effect`); the operand's half is read below, where the value
             // the declaration names is in hand.
             const auto &table = camel::core::OperatorTable::instance();
             if (table.effectOf(*oper) == core::sem::Effect::Pure) {
                 return std::nullopt;
             }
             // A higher-order operator is no effect of its own: what it touches is the function
             // the program hands it (design principles 3.3), read through
             // `functionValueClause` the way a call through a value is. A type that writes no
             // clause keeps the conservative answer: ordered, never folded away.
             if (const auto *facts = table.semantics(*oper);
                 facts != nullptr && facts->resources && facts->resources->functionOperand) {
                 const auto &where = *facts->resources->functionOperand;
                 if (where.kind == core::sem::ResourceBinding::Kind::Norm ||
                     where.kind == core::sem::ResourceBinding::Kind::With) {
                     const auto operands = where.kind == core::sem::ResourceBinding::Kind::Norm
                                               ? graph.inputs(ref, PortGroup::Normal)
                                               : graph.inputs(ref, PortGroup::Context);
                     const core::type::FunctionType *value = nullptr;
                     if (where.index < operands.size()) {
                         const core::type::Type *operand = graph.typeOf(operands[where.index]);
                         if (operand != nullptr &&
                             operand->code() == core::type::TypeCode::Function) {
                             value = static_cast<const core::type::FunctionType *>(operand);
                         }
                     }
                     if (const auto clause = core::functionValueClause(value)) {
                         if (clause->pure()) {
                             return std::nullopt;
                         }
                         return std::string(table.uri(*oper));
                     }
                 }
             }
             return std::string(table.uri(*oper));
         }});
    // Where branch arms meet: every arm must produce the type the JOIN carries.
    entries_.push_back(
        {{"branch-contract", "branch contract (arms agree on the result type)", "#d9d2e9"},
         [](const GraphView &graph, NodeRef ref) -> std::optional<std::string> {
             if (graph.kindOf(ref) != NodeKind::Brch) {
                 return std::nullopt;
             }
             // The JOIN and the BRCH are one kind in the view; the BRCH is the one that owns the
             // arms, and the JOIN that closes them owns nothing. A BRCH that has lost its arms is
             // read as a JOIN here -- that is a reading of a degraded graph, and this role is an
             // annotation on a dump, not a fact anything runs by.
             if (!graph.regionsOwnedBy(ref).empty()) {
                 return std::nullopt;
             }
             const core::type::Type *value = graph.typeOf(ref);
             return value != nullptr ? "arms: " + value->toString() : std::string("arms");
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
NodeRoleRegistry::classify(const GraphView &graph, NodeRef ref) const {
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