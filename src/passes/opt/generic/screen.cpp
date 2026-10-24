// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Screens of the generic optimization passes (see rewrite.h): conditions read from a frozen graph
 * that every rewrite of the pass needs, so that a graph without them is not opened as a draft.
 * Each one must stay at least as permissive as the pass it screens for: when a pass learns a new
 * rewrite, its screen has to admit the graphs that rewrite applies to.
 */

#include "rewrite.h"

#include "camel/core/operator_table.h"
#include "camel/core/tags.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

namespace camel::passes::generic {

using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;

namespace {

using camel::core::OperatorTable;
using camel::runtime::GCNode;
namespace type = camel::core::type;

/// What a frozen graph says of the value of node `ref` when a pass would take it for a constant:
/// the bits and type of a static DATA seen through GATEs (as the passes' staticValueOf does), or
/// `maybe` for what they read through a JOIN, which is not worked out here.
struct ConstantValue {
    bool known = false;
    bool maybe = false;
    slot_t slot = 0;
    const type::Type *type = nullptr;
};

ConstantValue constantValue(const GCGraph &graph, gc_node_ref_t ref) {
    for (size_t budget = 128; budget; --budget) {
        const GCNode *node = graph.node(ref);
        if (node == nullptr) {
            return {};
        }
        if (node->kind == GCNodeKind::Gate && graph.normInputsOf(ref).size() == 1) {
            ref = graph.normInputsOf(ref).front();
            continue;
        }
        if (node->kind == GCNodeKind::Join) {
            return {.maybe = true};
        }
        if (node->kind != GCNodeKind::Data || node->dataIndex >= 0) {
            return {};
        }
        const auto index = static_cast<size_t>(-node->dataIndex);
        if (index >= graph.staticSlots().size()) {
            return {};
        }
        const auto *layout = graph.staticDataType();
        const type::Type *ty =
            layout && index < layout->size() ? layout->typeAt(index) : node->dataType();
        return {.known = true, .slot = graph.staticSlots()[index], .type = ty};
    }
    return {};
}

bool isConstantLike(const GCGraph &graph, gc_node_ref_t ref) {
    const ConstantValue value = constantValue(graph, ref);
    return value.known || value.maybe;
}

bool isPureOperNode(const GCGraph &graph, gc_node_ref_t ref) {
    const GCNode *node = graph.node(ref);
    return node && node->kind == GCNodeKind::Oper && node->hasPayload() &&
           OperatorTable::instance().isPure(graph.nodeBodyAs<GCOperBody>(ref)->oper);
}

} // namespace

bool mayFold(const GCGraph &graph) {
    if (!graph.hasNodePayload()) {
        return true;
    }
    const auto &table = OperatorTable::instance();
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        const gc_node_ref_t ref = it.ref();
        const auto norm         = graph.normInputsOf(ref);
        const auto with         = graph.withInputsOf(ref);
        switch ((*it)->kind) {
        case GCNodeKind::Brch:
            if (norm.size() == 1 && isConstantLike(graph, norm[0])) {
                return true; // a static condition: the branch is replaced by its arm
            }
            break;
        case GCNodeKind::Accs:
            if (norm.size() == 1 && (graph.node(norm[0])->kind == GCNodeKind::Fill ||
                                     isConstantLike(graph, norm[0]))) {
                return true; // a projection of a tuple built here, or of a constant
            }
            break;
        case GCNodeKind::Fill:
            if (norm.size() == 1 && isConstantLike(graph, norm[0]) &&
                std::ranges::all_of(with, [&](gc_node_ref_t in) {
                    return isConstantLike(graph, in);
                })) {
                return true; // built from constants
            }
            break;
        case GCNodeKind::Cast:
            if (norm.size() == 1 && with.empty() && isConstantLike(graph, norm[0])) {
                return true; // a conversion of a constant
            }
            break;
        case GCNodeKind::Oper: {
            if (!isPureOperNode(graph, ref)) {
                break;
            }
            const auto isConstant = [&](gc_node_ref_t in) { return isConstantLike(graph, in); };
            if (std::ranges::all_of(with, isConstant) && std::ranges::all_of(norm, isConstant)) {
                return true; // all arguments are constants
            }
            if (with.empty() && table.typeFolder(graph.nodeBodyAs<GCOperBody>(ref)->oper)) {
                return true; // the argument types may fix the result
            }
            break;
        }
        default:
            break;
        }
    }
    return false;
}

bool mayMerge(const GCGraph &graph) {
    if (!graph.hasNodePayload()) {
        return true;
    }
    // Constants of equal type and bits are one value: a node with a known one is in the class of
    // (type, bits). Expressions are compared by the classes of their inputs, because merging the
    // constants is what makes expressions over separately written literals identical.
    constexpr uint64_t kClass = uint64_t{1} << 40;
    std::map<std::pair<const type::Type *, slot_t>, uint64_t> classes;
    std::vector<uint64_t> classOf(graph.nodeCount(), 0);
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        const gc_node_ref_t ref = it.ref();
        if ((*it)->kind == GCNodeKind::Join) {
            continue;
        }
        const ConstantValue value = constantValue(graph, ref);
        if (value.maybe) {
            return true;
        }
        if (!value.known || !value.type || type::isGCTraced(value.type->code())) {
            continue;
        }
        const auto [at, inserted] =
            classes.try_emplace({value.type, value.slot}, kClass + classes.size());
        if (!inserted) {
            return true; // two nodes with the same constant
        }
        classOf[ref] = at->second;
    }
    std::set<std::vector<uint64_t>> seen;
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        const gc_node_ref_t ref = it.ref();
        if (!isPureOperNode(graph, ref)) {
            continue;
        }
        std::vector<uint64_t> key{
            graph.nodeBodyAs<GCOperBody>(ref)->oper,
            (*it)->typeId,
            camel::core::tag::permissionsOf(graph, ref)};
        for (auto inputs : {graph.normInputsOf(ref), graph.withInputsOf(ref)}) {
            key.push_back(~uint64_t{0});
            for (gc_node_ref_t in : inputs) {
                key.push_back(classOf[in] ? classOf[in] : in);
            }
        }
        if (!seen.insert(std::move(key)).second) {
            return true; // two identical expressions
        }
    }
    return false;
}

bool mayRemoveDead(const GCGraph &graph) {
    if (!graph.hasNodePayload()) {
        return true;
    }
    const size_t count = graph.nodeCount();
    // Per node: the value users (norm and with), whether one of them is a JOIN or a BRCH, and
    // whether the node anchors the graph.
    std::vector<uint32_t> valueUsers(count, 0);
    std::vector<bool> joinOrBrchUser(count, false), anchor(count, false);
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        const gc_node_ref_t ref = it.ref();
        const bool joinOrBrch   = (*it)->kind == GCNodeKind::Join || (*it)->kind == GCNodeKind::Brch;
        for (auto inputs : {graph.normInputsOf(ref), graph.withInputsOf(ref)}) {
            for (gc_node_ref_t in : inputs) {
                ++valueUsers[in];
                joinOrBrchUser[in] = joinOrBrchUser[in] || joinOrBrch;
            }
        }
    }
    for (gc_node_ref_t end : {graph.exitNodeRef(), graph.outputNodeRef(), graph.returnNodeRef()}) {
        if (end < count) {
            anchor[end] = true;
        }
    }
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        const gc_node_ref_t ref = it.ref();
        const GCNode *node      = *it;
        if (anchor[ref]) {
            continue;
        }
        // A gate that waits and forwards its value hands its waits to its consumers.
        if (node->kind == GCNodeKind::Gate && graph.normInputsOf(ref).size() == 1 &&
            graph.withInputsOf(ref).empty() && !graph.ctrlInputsOf(ref).empty() &&
            valueUsers[ref] > 0 && !joinOrBrchUser[ref]) {
            return true;
        }
        // What nothing reads: a SYNC, a GATE or a value computed for nothing.
        if (valueUsers[ref] != 0) {
            continue;
        }
        switch (node->kind) {
        case GCNodeKind::Sync:
        case GCNodeKind::Gate:
        case GCNodeKind::Cast:
        case GCNodeKind::Copy:
        case GCNodeKind::Accs:
        case GCNodeKind::Fill:
            return true;
        case GCNodeKind::Data:
            if (node->dataIndex < 0) {
                return true;
            }
            break;
        case GCNodeKind::Oper:
            if (isPureOperNode(graph, ref)) {
                return true;
            }
            break;
        default:
            break;
        }
    }
    return false;
}

bool mayReduceOrder(const GCGraph &graph) {
    if (!graph.hasNodePayload()) {
        return true;
    }
    for (auto it = graph.nodes().begin(); it != graph.nodes().end(); ++it) {
        const gc_node_ref_t ref = it.ref();
        // Several order edges meeting in one node: one of them may be implied by a path through
        // the others (docs/design/effects/edges.cn.md 5.4). The pass reads the same condition.
        if (graph.orderPredsOf(ref).size() >= 2) {
            return true;
        }
        const size_t ctrl = graph.ctrlInputsOf(ref).size();
        // Redundant control inputs: several, or one that a value input makes unnecessary. The
        // control-edge kind is still in the graph, so the pass still reduces it.
        if (ctrl >= 2 ||
            (ctrl == 1 && (!graph.normInputsOf(ref).empty() || !graph.withInputsOf(ref).empty()))) {
            return true;
        }
    }
    return false;
}

} // namespace camel::passes::generic
