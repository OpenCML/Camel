// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Node role registry and the runtime's own roles (see node_roles.h). Both roles are written over the
 * view; the effect role reads the fact layer's answer for the node instead of working it out, and a
 * fact is anchored on a graph a store holds, so that role answers nothing for a draft.
 */

#include "camel/runtime/node_roles.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/base.h"
#include "camel/runtime/core_facts.h"
#include "camel/runtime/facts.h"
#include "camel/runtime/program.h"
#include "camel/runtime/resources.h"

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
         [](const NodeRoleSite &site, NodeRef ref) -> std::optional<std::string> {
             const std::optional<camel::core::OperId> oper = operatorOf(site.view, ref);
             if (!oper.has_value()) {
                 return std::nullopt;
             }
             // The node's resources, asked of the fact layer (`core.resources`): the accesses the
             // operator's record states united with the ones its function operand's clause brings,
             // what it allocates, and whether it reaches outside the program (design principles
             // 3.3). A node that touches nothing is no effect; every other answer is one, and the
             // fact says which. The union this role used to spell out here was the same rule as a
             // third predicate, kept in step by hand (architecture 2.3).
             if (site.graph == nullptr || site.program == nullptr || site.facts == nullptr) {
                 // No store holds this graph (a draft), so there is no fact to read: the role says
                 // nothing rather than working the union out a second time.
                 return std::nullopt;
             }
             const std::optional<NodeResources> resources =
                 fact::nodeResourcesOf(*site.facts, *site.program, *site.graph, ref);
             if (!resources.has_value()) {
                 return std::nullopt;
             }
             return std::string(camel::core::OperatorTable::instance().uri(*oper));
         }});
    // Where branch arms meet: every arm must produce the type the JOIN carries.
    entries_.push_back(
        {{"branch-contract", "branch contract (arms agree on the result type)", "#d9d2e9"},
         [](const NodeRoleSite &site, NodeRef ref) -> std::optional<std::string> {
             if (site.view.kindOf(ref) != NodeKind::Brch) {
                 return std::nullopt;
             }
             // The JOIN and the BRCH are one kind in the view; the BRCH is the one that owns the
             // arms, and the JOIN that closes them owns nothing. A BRCH that has lost its arms is
             // read as a JOIN here -- that is a reading of a degraded graph, and this role is an
             // annotation on a dump, not a fact anything runs by.
             if (!site.view.regionsOwnedBy(ref).empty()) {
                 return std::nullopt;
             }
             const core::type::Type *value = site.view.typeOf(ref);
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
NodeRoleRegistry::classify(const NodeRoleSite &site, NodeRef ref) const {
    std::shared_lock lock(mutex_);
    std::vector<Match> matches;
    for (const Entry &e : entries_) {
        if (auto detail = e.classify(site, ref)) {
            matches.push_back({e.spec, std::move(*detail)});
        }
    }
    return matches;
}

} // namespace camel::runtime