// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The consistency check of the view (`consistency.h`): the comparisons, in the order their lines are
 * written, and the two readings each line prints.
 */

#include "graph/consistency.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

namespace camel::graph {
namespace {

/// One letter per kind, so a reading can print a graph's node kinds in node order on one line
/// without a table of words. A kind the view does not have never appears; a gap (a slot the graph
/// answers nothing about) prints as a dot.
char kindLetter(NodeKind kind) {
    switch (kind) {
    case NodeKind::Data:
        return 'D';
    case NodeKind::Port:
        return 'P';
    case NodeKind::Oper:
        return 'O';
    case NodeKind::Func:
        return 'U';
    case NodeKind::Call:
        return 'L';
    case NodeKind::Bind:
        return 'N';
    case NodeKind::Brch:
        return 'B';
    case NodeKind::Loop:
        return 'W';
    case NodeKind::Gate:
        return 'G';
    }
    return '?';
}

std::string listText(const std::vector<NodeRef> &list) {
    std::string text = "[";
    for (std::size_t i = 0; i < list.size(); ++i) {
        if (i != 0) {
            text += ',';
        }
        text += std::format("{}", list[i]);
    }
    text += ']';
    return text;
}

/// The disagreement, when two lists of the same question are not the same list.
std::string listDifference(
    NodeRef ref, const char *what, const std::vector<NodeRef> &draft,
    const std::vector<NodeRef> &frozen) {
    if (draft == frozen) {
        return {};
    }
    return std::format(
        "node {} {}: draft {} frozen {}",
        ref,
        what,
        listText(draft),
        listText(frozen));
}

/// A type is compared by identity first and by spelling second: a graph's own function type is an
/// object of that graph, and two objects that print the same are the same type here, while a type
/// that prints differently is a difference whatever its pointer.
bool sameType(const camel::core::type::Type *draft, const camel::core::type::Type *frozen) {
    if (draft == frozen) {
        return true;
    }
    if (draft == nullptr || frozen == nullptr) {
        return false;
    }
    return draft->toString() == frozen->toString();
}

std::string typeText(const camel::core::type::Type *type) {
    return type == nullptr ? "<none>" : type->toString();
}

/// One region of each reading, field for field. The record is a value on both sides, so the fields
/// are compared one by one to keep the line that reports a difference able to name the field.
std::string regionDifference(const GraphView &draft, const GraphView &frozen, RegionId id) {
    // A region with no node in it is a place neither reading can be taken through: a draft keeps a
    // region for every arm and block it ever made, while an encoding writes the ones its nodes stand
    // in, so the two need not agree about a place nothing stands in. A region that holds a node on
    // either side is compared -- and if a node stands in a different region on the two sides, the
    // per-node comparison below has already said so.
    if (draft.regionNodes(id).empty() && frozen.regionNodes(id).empty()) {
        return {};
    }
    const RegionRecord left  = draft.region(id);
    const RegionRecord right = frozen.region(id);
    if (left.kind != right.kind) {
        return std::format(
            "region {}: kind draft={} frozen={} nodes draft={} frozen={}",
            id,
            static_cast<int>(left.kind),
            static_cast<int>(right.kind),
            draft.regionNodes(id).size(),
            frozen.regionNodes(id).size());
    }
    if (left.owner != right.owner) {
        return std::format("region {}: owner draft={} frozen={}", id, left.owner, right.owner);
    }
    if (left.index != right.index) {
        return std::format("region {}: index draft={} frozen={}", id, left.index, right.index);
    }
    if (left.parent != right.parent) {
        return std::format("region {}: parent draft={} frozen={}", id, left.parent, right.parent);
    }
    if (left.result != right.result) {
        return std::format("region {}: result draft={} frozen={}", id, left.result, right.result);
    }
    return {};
}

} // namespace

std::string firstDifference(const GraphView &draft, const GraphView &frozen) {
    // The graph's own facts, before its nodes: the regions it has.
    if (draft.regionCount() != frozen.regionCount()) {
        return std::format(
            "regions: draft has {}, frozen has {}",
            draft.regionCount(),
            frozen.regionCount());
    }
    if (draft.nodeSlotCount() != frozen.nodeSlotCount()) {
        return std::format(
            "node slots: draft has {}, frozen has {}",
            draft.nodeSlotCount(),
            frozen.nodeSlotCount());
    }
    for (RegionId id = 0; id < draft.regionCount(); ++id) {
        // A place nothing stands in is not compared: see regionDifference.
        if (draft.regionNodes(id).empty() && frozen.regionNodes(id).empty()) {
            continue;
        }
        if (std::string difference; !(difference = regionDifference(draft, frozen, id)).empty()) {
            return difference;
        }
        // The parameters of a region are its ports: the state of a loop, the values an arm is
        // given. Compared as a list, not as a count, because which value a region is given is the
        // fact.
        const std::vector<NodeRef> left(draft.regionParameters(id).begin(),
                                        draft.regionParameters(id).end());
        const std::vector<NodeRef> right(frozen.regionParameters(id).begin(),
                                         frozen.regionParameters(id).end());
        if (left != right) {
            return std::format(
                "region {}: parameters draft {} frozen {}",
                id,
                listText(left),
                listText(right));
        }
    }
    for (NodeRef ref = 0; ref < draft.nodeSlotCount(); ++ref) {
        if (draft.contains(ref) != frozen.contains(ref)) {
            return std::format(
                "node {}: present draft={} frozen={}",
                ref,
                draft.contains(ref),
                frozen.contains(ref));
        }
        if (!draft.contains(ref)) {
            continue;
        }
        // The region a node is in belongs to the graph the node is in, so it is compared beside the
        // record rather than in it. It is also what a control input used to say (`with`/ctrl runs):
        // the arm a node runs in, which is why the check does not compare a control-input list --
        // the view has no such list, and this is the same fact.
        if (draft.regionOf(ref) != frozen.regionOf(ref)) {
            return std::format(
                "node {}: region draft={} frozen={}",
                ref,
                draft.regionOf(ref),
                frozen.regionOf(ref));
        }
        if (draft.kindOf(ref) != frozen.kindOf(ref)) {
            return std::format(
                "node {}: kind draft={} frozen={}",
                ref,
                kindLetter(draft.kindOf(ref)),
                kindLetter(frozen.kindOf(ref)));
        }
        if (!sameType(draft.typeOf(ref), frozen.typeOf(ref))) {
            return std::format(
                "node {}: type draft={} frozen={}",
                ref,
                typeText(draft.typeOf(ref)),
                typeText(frozen.typeOf(ref)));
        }
        // A constant is a DATA node whose value is in the graph's static area: the view answers its
        // value and nothing else about that area (the file comment).
        const std::optional<::slot_t> leftConstant  = draft.constantOf(ref);
        const std::optional<::slot_t> rightConstant = frozen.constantOf(ref);
        if (leftConstant.has_value() != rightConstant.has_value()) {
            return std::format(
                "node {}: constant draft={} frozen={}",
                ref,
                leftConstant.has_value() ? "value" : "none",
                rightConstant.has_value() ? "value" : "none");
        }
        if (std::string difference;
            !(difference = listDifference(
                  ref,
                  "normal inputs",
                  std::vector<NodeRef>(draft.inputs(ref, PortGroup::Normal).begin(),
                                       draft.inputs(ref, PortGroup::Normal).end()),
                  std::vector<NodeRef>(frozen.inputs(ref, PortGroup::Normal).begin(),
                                       frozen.inputs(ref, PortGroup::Normal).end())))
                 .empty()) {
            return difference;
        }
        if (std::string difference;
            !(difference = listDifference(
                  ref,
                  "context inputs",
                  std::vector<NodeRef>(draft.inputs(ref, PortGroup::Context).begin(),
                                       draft.inputs(ref, PortGroup::Context).end()),
                  std::vector<NodeRef>(frozen.inputs(ref, PortGroup::Context).begin(),
                                       frozen.inputs(ref, PortGroup::Context).end())))
                 .empty()) {
            return difference;
        }
        // The two side arrays of the frozen graph and the draft's two, asked as the edges they are:
        // a statement waits for the one written before it in its `sync` block (Sync) and for the
        // one that must run first where both run (Order). Compared as multisets, not as sequences:
        // neither array is ordered -- two order edges say "runs before", and which of the two a
        // representation lists first is its own bookkeeping (the encoder writes them in the order
        // its table holds them, the draft in the order the builder pushed them). Only the data
        // edges are compared as lists, because a port's position is part of the answer there.
        for (const EdgeKind kind : {EdgeKind::Sync, EdgeKind::Order}) {
            const char *what = kind == EdgeKind::Sync ? "sync predecessors" : "order predecessors";
            std::vector<NodeRef> left  = draft.predecessors(ref, kind);
            std::vector<NodeRef> right = frozen.predecessors(ref, kind);
            std::sort(left.begin(), left.end());
            std::sort(right.begin(), right.end());
            if (std::string difference;
                !(difference = listDifference(ref, what, left, right)).empty()) {
                return difference;
            }
        }
        // The operator the node names: `kNoOper` for every kind that has no record in either
        // representation, and one global id for an OPER node, so the two readings agree or name two
        // operators. Asked of every node rather than only of `NodeKind::Oper`, because the four
        // kinds that collapse into it answer `kNoOper` on both sides and that agreement is worth
        // having on record -- a node that gains a record on one side only shows up here.
        if (draft.operatorOf(ref) != frozen.operatorOf(ref)) {
            // By URI, not by id: a difference has to read as two operators, and the id of the one
            // side says nothing to a reader of the report.
            const auto uri = [](camel::core::OperId oper) {
                return oper == camel::core::kNoOper
                           ? std::string("<none>")
                           : std::string(camel::core::OperatorTable::instance().uri(oper));
            };
            return std::format(
                "node {}: operator draft={} frozen={}",
                ref,
                uri(draft.operatorOf(ref)),
                uri(frozen.operatorOf(ref)));
        }
        const CalleeRef leftCallee  = draft.calleeOf(ref);
        const CalleeRef rightCallee = frozen.calleeOf(ref);
        if (leftCallee.kind != rightCallee.kind) {
            return std::format(
                "node {}: callee draft kind={} frozen kind={}",
                ref,
                static_cast<int>(leftCallee.kind),
                static_cast<int>(rightCallee.kind));
        }
        if (leftCallee.kind == CalleeRef::Kind::Name && leftCallee.name != rightCallee.name) {
            return std::format("node {}: callee name differs", ref);
        }
        // The regions a structural node owns: a BRCH's arms and a LOOP's condition and body. This
        // is what a BRCH's arm count and its JOIN used to be, in one answer.
        if (draft.regionsOwnedBy(ref) != frozen.regionsOwnedBy(ref)) {
            return std::format(
                "node {}: regions owned draft={} frozen={}",
                ref,
                listText(std::vector<NodeRef>(
                    draft.regionsOwnedBy(ref).begin(), draft.regionsOwnedBy(ref).end())),
                listText(std::vector<NodeRef>(
                    frozen.regionsOwnedBy(ref).begin(), frozen.regionsOwnedBy(ref).end())));
        }
        const LoopForm leftLoop  = draft.loopFormOf(ref);
        const LoopForm rightLoop = frozen.loopFormOf(ref);
        if (leftLoop.kind != rightLoop.kind) {
            return std::format(
                "node {}: loop kind draft={} frozen={}",
                ref,
                static_cast<int>(leftLoop.kind),
                static_cast<int>(rightLoop.kind));
        }
        if (leftLoop.iterableForm != rightLoop.iterableForm) {
            return std::format(
                "node {}: loop iterable form draft={} frozen={}",
                ref,
                static_cast<int>(leftLoop.iterableForm),
                static_cast<int>(rightLoop.iterableForm));
        }
        if (leftLoop.collectionCount != rightLoop.collectionCount) {
            return std::format(
                "node {}: loop collection count draft={} frozen={}",
                ref,
                leftLoop.collectionCount,
                rightLoop.collectionCount);
        }
    }
    return {};
}

std::string describe(const GraphView &graph) {
    std::string kinds;
    kinds.reserve(static_cast<std::size_t>(graph.nodeSlotCount()));
    std::uint32_t present = 0;
    for (NodeRef ref = 0; ref < graph.nodeSlotCount(); ++ref) {
        if (!graph.contains(ref)) {
            kinds.push_back('.');
            continue;
        }
        kinds.push_back(kindLetter(graph.kindOf(ref)));
        ++present;
    }
    return std::format(
        "nodes={} present={} regions={} kinds={}",
        graph.nodeSlotCount(),
        present,
        graph.regionCount(),
        kinds);
}

} // namespace camel::graph