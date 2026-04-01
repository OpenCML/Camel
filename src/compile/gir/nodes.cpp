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
 * Author: Zhenjie Wei
 * Created: Aug. 17, 2024
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir/nodes.h"
#include "camel/compile/gir/arena.h"
#include "camel/compile/gir/builder.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/rtdata/func.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"
#include <functional>
#include <sstream>

namespace camel::compile::gir {

namespace {

TupleType *currentClosureTupleType(const Graph *graph) {
    ASSERT(graph != nullptr, "Closure tuple type source graph is null.");
    type_vec_t closureTypes;
    closureTypes.reserve(graph->closure().size());
    for (Node *node : graph->closure()) {
        closureTypes.push_back(node->dataType());
    }
    return TupleType::create(std::move(closureTypes));
}

inline void assertDraftMutable(const Node *node, const char *action) {
    ASSERT(node != nullptr, "Node is null.");
    (void)action;
    ASSERT(!node->graph().finalized(), "Cannot mutate sealed graph node.");
}
template <typename T, typename... Args> T *makeOwnedNode(Graph &graph, Args &&...args) {
    auto arena = graph.arena();
    ASSERT(arena != nullptr, "Graph arena is null.");
    // Node 本体直接进入 FrozenRegion，确保 seal 后 releaseDraftRegion 不影响节点生命周期。
    Node *owned = GraphBuilder(graph).ownNode(arena->constructTrackedInRegion<T>(
        GraphArena::Region::Frozen,
        std::forward<Args>(args)...));
    return tt::as_ptr<T>(owned);
}

} // namespace

// =============================================================================
// Node freeze 支持
// =============================================================================

void Node::freezeAdjacency(GraphArena &arena) {
    ASSERT(!frozen_, "Node is already frozen.");

    auto copyToArena = [&](const node_vec_t &vec) -> FrzAdj {
        if (vec.empty())
            return {};
        auto n  = static_cast<uint16_t>(vec.size());
        auto *p = static_cast<Node **>(arena.allocFrozen(sizeof(Node *) * n, alignof(Node *)));
        std::memcpy(p, vec.data(), sizeof(Node *) * n);
        return {p, n};
    };

    frzNormIn_  = copyToArena(normInputs_);
    frzWithIn_  = copyToArena(withInputs_);
    frzCtrlIn_  = copyToArena(ctrlInputs_);
    frzNormOut_ = copyToArena(normOutputs_);
    frzWithOut_ = copyToArena(withOutputs_);
    frzCtrlOut_ = copyToArena(ctrlOutputs_);

    // 释放 draft vectors 的堆内存
    normInputs_  = {};
    withInputs_  = {};
    ctrlInputs_  = {};
    normOutputs_ = {};
    withOutputs_ = {};
    ctrlOutputs_ = {};
    frozen_      = true;
}

// =============================================================================
// Node 索引与连通性
// =============================================================================

data_idx_t Node::index() const {
    ASSERT(nodeType_ != NodeType::SYNC, "SYNC node has no data index.");
    ASSERT(nodeType_ != NodeType::DREF, "DREF node has no data index.");
    if (nodeType_ == NodeType::GATE) {
        ASSERT(!normInputs().empty(), "GATE node must have one norm input before reading index.");
        return normInputs().front()->index();
    }
    return dataIndex_;
}

std::string Node::toString() const {
    return std::format("Node({}, {})", to_string(nodeType_), std::to_string(dataIndex_));
}

Node::operator std::string() const { return toString(); }

node_vec_t Node::dataInputs() const {
    auto ni = normInputs(), wi = withInputs();
    node_vec_t inputs;
    inputs.reserve(ni.size() + wi.size());
    inputs.insert(inputs.end(), ni.begin(), ni.end());
    inputs.insert(inputs.end(), wi.begin(), wi.end());
    return inputs;
}

node_vec_t Node::inputs() const {
    auto ni = normInputs(), wi = withInputs(), ci = ctrlInputs();
    node_vec_t inputs;
    inputs.reserve(ni.size() + wi.size() + ci.size());
    inputs.insert(inputs.end(), ni.begin(), ni.end());
    inputs.insert(inputs.end(), wi.begin(), wi.end());
    inputs.insert(inputs.end(), ci.begin(), ci.end());
    return inputs;
}

node_vec_t Node::dataOutputs() const {
    auto no = normOutputs(), wo = withOutputs();
    node_vec_t outputs;
    outputs.reserve(no.size() + wo.size());
    outputs.insert(outputs.end(), no.begin(), no.end());
    outputs.insert(outputs.end(), wo.begin(), wo.end());
    return outputs;
}

node_vec_t Node::outputs() const {
    auto no = normOutputs(), wo = withOutputs(), co = ctrlOutputs();
    node_vec_t outputs;
    outputs.reserve(no.size() + wo.size() + co.size());
    outputs.insert(outputs.end(), no.begin(), no.end());
    outputs.insert(outputs.end(), wo.begin(), wo.end());
    outputs.insert(outputs.end(), co.begin(), co.end());
    return outputs;
}

bool Node::hasDeepLinkedTo(Node *node, size_t maxJumps) const {
    if (maxJumps == 0) {
        return false;
    }

    std::unordered_set<Node *> visited;
    std::function<bool(Node *, size_t)> dfs;

    dfs = [&](Node *current, size_t jumpsLeft) -> bool {
        ASSERT(current, "Current node is null in DFS.");
        if (jumpsLeft == 0) {
            CAMEL_LOG_WARN_S(
                "GIR",
                "Deep link check reached max jumps at node: {}.",
                node->toString());
            return false;
        }

        visited.insert(current);

        for (auto *out : current->withOutputs()) {
            if (out == node)
                return true;
            if (visited.find(out) == visited.end()) {
                if (dfs(out, jumpsLeft - 1))
                    return true;
            }
        }
        for (auto *out : current->normOutputs()) {
            if (out == node)
                return true;
            if (visited.find(out) == visited.end()) {
                if (dfs(out, jumpsLeft - 1))
                    return true;
            }
        }
        for (auto *out : current->ctrlOutputs()) {
            if (out == node)
                return true;
            if (visited.find(out) == visited.end()) {
                if (dfs(out, jumpsLeft - 1))
                    return true;
            }
        }
        return false;
    };

    for (auto *out : withOutputs()) {
        if (dfs(out, maxJumps - 1))
            return true;
    }
    for (auto *out : normOutputs()) {
        if (dfs(out, maxJumps - 1))
            return true;
    }
    for (auto *out : ctrlOutputs()) {
        if (dfs(out, maxJumps - 1))
            return true;
    }
    return false;
}

bool Node::hasLinkedTo(Node *node) const {
    for (auto *out : withOutputs()) {
        if (out == node)
            return true;
    }
    for (auto *out : normOutputs()) {
        if (out == node)
            return true;
    }
    for (auto *out : ctrlOutputs()) {
        if (out == node)
            return true;
    }
    return false;
}

JoinNode *Node::matchedJoinOutput() const {
    ASSERT(hasMatchedJoinOutput(), "Node does not have a single matched JOIN output.");
    return tt::as_ptr<JoinNode>(withOutputs().front());
}

// =============================================================================
// Node 连边与替换
// =============================================================================

void Node::link(LinkType type, Node *from, Node *to) {
    assertDraftMutable(from, "link");
    assertDraftMutable(to, "link");
    ASSERT(!from->frozen_ && !to->frozen_, "Cannot mutate frozen nodes.");
    ASSERT(
        from->nodeType_ != NodeType::DREF,
        "DREF nodes cannot be linked as input to other nodes.");
    ASSERT(from && to, "Cannot link null nodes.");
    ASSERT(from != to, "Cannot link a node to itself.");
    ASSERT(
        &from->graph() == &to->graph(),
        std::format(
            "Cannot link nodes from different graphs: {} -{}-> {}.",
            from->toString(),
            (type == LinkType::With ? "W" : (type == LinkType::Norm ? "N" : "C")),
            to->toString()));

    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "GIR",
        "Linking nodes: {} -{}-> {}",
        from->toString(),
        (type == LinkType::With ? "W" : (type == LinkType::Norm ? "N" : "C")),
        to->toString()));

    switch (type) {
    case LinkType::With:
        from->withOutputs_.push_back(to);
        to->withInputs_.push_back(from);
        break;
    case LinkType::Norm:
        from->normOutputs_.push_back(to);
        to->normInputs_.push_back(from);
        break;
    case LinkType::Ctrl:
        ASSERT(
            std::find(from->ctrlOutputs_.begin(), from->ctrlOutputs_.end(), to) ==
                from->ctrlOutputs_.end(),
            "Nodes are already linked (ctrl).");
        from->ctrlOutputs_.push_back(to);
        to->ctrlInputs_.push_back(from);
        break;
    }
}

bool Node::unlink(Node *from, Node *to) {
    assertDraftMutable(from, "unlink");
    assertDraftMutable(to, "unlink");
    ASSERT(from && to, "Cannot unlink null nodes.");
    ASSERT(from != to, "Cannot unlink a node from itself.");
    ASSERT(
        &from->graph() == &to->graph(),
        std::format(
            "Cannot unlink nodes from different graphs: {} -X- {}.",
            from->toString(),
            to->toString()));

    EXEC_WHEN_DEBUG(
        CAMEL_LOG_DEBUG_S("GIR", "Unlinking nodes: {} -X- {}", from->toString(), to->toString()));

    auto &toNormInputs = to->normInputs_;
    if (std::find(toNormInputs.begin(), toNormInputs.end(), from) != toNormInputs.end()) {
        toNormInputs.erase(
            std::remove(toNormInputs.begin(), toNormInputs.end(), from),
            toNormInputs.end());
        auto &fromNormOutputs = from->normOutputs_;
        fromNormOutputs.erase(
            std::remove(fromNormOutputs.begin(), fromNormOutputs.end(), to),
            fromNormOutputs.end());
        return true;
    }

    auto &toWithInputs = to->withInputs_;
    if (std::find(toWithInputs.begin(), toWithInputs.end(), from) != toWithInputs.end()) {
        toWithInputs.erase(
            std::remove(toWithInputs.begin(), toWithInputs.end(), from),
            toWithInputs.end());
        auto &fromWithOutputs = from->withOutputs_;
        fromWithOutputs.erase(
            std::remove(fromWithOutputs.begin(), fromWithOutputs.end(), to),
            fromWithOutputs.end());
        return true;
    }

    auto &toCtrlInputs = to->ctrlInputs_;
    if (std::find(toCtrlInputs.begin(), toCtrlInputs.end(), from) != toCtrlInputs.end()) {
        toCtrlInputs.erase(
            std::remove(toCtrlInputs.begin(), toCtrlInputs.end(), from),
            toCtrlInputs.end());
        auto &fromCtrlOutputs = from->ctrlOutputs_;
        fromCtrlOutputs.erase(
            std::remove(fromCtrlOutputs.begin(), fromCtrlOutputs.end(), to),
            fromCtrlOutputs.end());
        return true;
    }

    ASSERT(false, "Try to unlink nodes that are not linked.");
    return false;
}

bool Node::unlinkCtrl(Node *from, Node *to) {
    assertDraftMutable(from, "unlink ctrl edge");
    assertDraftMutable(to, "unlink ctrl edge");
    ASSERT(from && to, "Cannot unlink null nodes.");
    ASSERT(from != to, "Cannot unlink a node from itself.");
    ASSERT(
        &from->graph() == &to->graph(),
        std::format(
            "Cannot unlink ctrl nodes from different graphs: {} -X- {}.",
            from->toString(),
            to->toString()));

    auto &toCtrlInputs = to->ctrlInputs_;
    auto ctrlIt        = std::find(toCtrlInputs.begin(), toCtrlInputs.end(), from);
    if (ctrlIt == toCtrlInputs.end()) {
        return false;
    }
    toCtrlInputs.erase(ctrlIt);
    auto &fromCtrlOutputs = from->ctrlOutputs_;
    fromCtrlOutputs.erase(
        std::remove(fromCtrlOutputs.begin(), fromCtrlOutputs.end(), to),
        fromCtrlOutputs.end());
    return true;
}

namespace {

node_vec_t &selectInputs(LinkType type, Node *node) {
    switch (type) {
    case LinkType::With:
        return detail::NodeMutation::withInputs(node);
    case LinkType::Norm:
        return detail::NodeMutation::normInputs(node);
    case LinkType::Ctrl:
        return detail::NodeMutation::ctrlInputs(node);
    }
    ASSERT(false, "Unsupported link type when selecting inputs.");
    return detail::NodeMutation::normInputs(node);
}

node_vec_t &selectOutputs(LinkType type, Node *node) {
    switch (type) {
    case LinkType::With:
        return detail::NodeMutation::withOutputs(node);
    case LinkType::Norm:
        return detail::NodeMutation::normOutputs(node);
    case LinkType::Ctrl:
        return detail::NodeMutation::ctrlOutputs(node);
    }
    ASSERT(false, "Unsupported link type when selecting outputs.");
    return detail::NodeMutation::normOutputs(node);
}

} // namespace

void Node::replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput) {
    assertDraftMutable(owner, "replace input");
    assertDraftMutable(oldInput, "replace input");
    assertDraftMutable(newInput, "replace input");
    ASSERT(owner && oldInput && newInput, "Cannot replace input with null nodes.");
    ASSERT(&owner->graph() == &oldInput->graph(), "Old input belongs to a different graph.");
    ASSERT(&owner->graph() == &newInput->graph(), "New input belongs to a different graph.");
    if (oldInput == newInput) {
        return;
    }

    // BRCH/JOIN 等节点会把边序当作槽位语义，不能用 unlink+link 破坏原位置。
    auto &inputs     = selectInputs(type, owner);
    auto &oldOutputs = selectOutputs(type, oldInput);
    auto &newOutputs = selectOutputs(type, newInput);
    auto ownerIt     = std::find(inputs.begin(), inputs.end(), oldInput);
    ASSERT(ownerIt != inputs.end(), "Old input edge not found when replacing input.");
    size_t ownerIndex = static_cast<size_t>(ownerIt - inputs.begin());

    const bool preserveBranchSlots = (type == LinkType::With && owner->type() == NodeType::JOIN) ||
                                     (type == LinkType::Ctrl && owner->type() == NodeType::BRCH);
    if (!preserveBranchSlots) {
        auto existingIt = std::find(inputs.begin(), inputs.end(), newInput);
        if (existingIt != inputs.end()) {
            size_t existingIndex = static_cast<size_t>(existingIt - inputs.begin());
            inputs.erase(existingIt);
            if (auto newOutIt = std::find(newOutputs.begin(), newOutputs.end(), owner);
                newOutIt != newOutputs.end()) {
                newOutputs.erase(newOutIt);
            }
            if (existingIndex < ownerIndex) {
                ownerIndex--;
            }
        }
    }

    inputs[ownerIndex] = newInput;
    if (auto oldOutIt = std::find(oldOutputs.begin(), oldOutputs.end(), owner);
        oldOutIt != oldOutputs.end()) {
        oldOutputs.erase(oldOutIt);
    }
    if (std::find(newOutputs.begin(), newOutputs.end(), owner) == newOutputs.end()) {
        newOutputs.push_back(owner);
    }
}

void Node::replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput) {
    assertDraftMutable(owner, "replace output");
    assertDraftMutable(oldOutput, "replace output");
    assertDraftMutable(newOutput, "replace output");
    ASSERT(owner && oldOutput && newOutput, "Cannot replace output with null nodes.");
    ASSERT(&owner->graph() == &oldOutput->graph(), "Old output belongs to a different graph.");
    ASSERT(&owner->graph() == &newOutput->graph(), "New output belongs to a different graph.");
    if (oldOutput == newOutput) {
        return;
    }

    // 这里保留 owner 侧的输出槽位顺序，避免控制分支编号在 rewrite 之后漂移。
    auto &outputs   = selectOutputs(type, owner);
    auto &oldInputs = selectInputs(type, oldOutput);
    auto &newInputs = selectInputs(type, newOutput);
    auto ownerIt    = std::find(outputs.begin(), outputs.end(), oldOutput);
    ASSERT(ownerIt != outputs.end(), "Old output edge not found when replacing output.");
    size_t ownerIndex = static_cast<size_t>(ownerIt - outputs.begin());

    const bool preserveBranchSlots = (type == LinkType::Ctrl && owner->type() == NodeType::BRCH) ||
                                     (type == LinkType::With && owner->type() == NodeType::JOIN);
    if (!preserveBranchSlots) {
        auto existingIt = std::find(outputs.begin(), outputs.end(), newOutput);
        if (existingIt != outputs.end()) {
            size_t existingIndex = static_cast<size_t>(existingIt - outputs.begin());
            outputs.erase(existingIt);
            if (auto newInIt = std::find(newInputs.begin(), newInputs.end(), owner);
                newInIt != newInputs.end()) {
                newInputs.erase(newInIt);
            }
            if (existingIndex < ownerIndex) {
                ownerIndex--;
            }
        }
    }

    outputs[ownerIndex] = newOutput;
    if (auto oldInIt = std::find(oldInputs.begin(), oldInputs.end(), owner);
        oldInIt != oldInputs.end()) {
        oldInputs.erase(oldInIt);
    }
    if (std::find(newInputs.begin(), newInputs.end(), owner) == newInputs.end()) {
        newInputs.push_back(owner);
    }
}

bool Node::replace(Node *oldNode, Node *newNode) {
    assertDraftMutable(oldNode, "replace node");
    assertDraftMutable(newNode, "replace node");
    ASSERT(oldNode && newNode, "Cannot replace null nodes.");
    ASSERT(oldNode != newNode, "Cannot replace a node with itself.");
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "GIR",
        "Replacing node: {} -> {}",
        oldNode->toString(),
        newNode->toString()));

    const node_vec_t withInputs(oldNode->withInputs().begin(), oldNode->withInputs().end());
    const node_vec_t normInputs(oldNode->normInputs().begin(), oldNode->normInputs().end());
    const node_vec_t ctrlInputs(oldNode->ctrlInputs().begin(), oldNode->ctrlInputs().end());
    const node_vec_t withOutputs(oldNode->withOutputs().begin(), oldNode->withOutputs().end());
    const node_vec_t normOutputs(oldNode->normOutputs().begin(), oldNode->normOutputs().end());
    const node_vec_t ctrlOutputs(oldNode->ctrlOutputs().begin(), oldNode->ctrlOutputs().end());

    // 整节点替换与单边替换必须复用同一套保序语义，否则 BRCH/JOIN/CALL 这类依赖槽位
    // 顺序的节点会在“节点级 rewrite”里悄悄漂移。
    for (Node *in : withInputs) {
        Node::replaceOutput(LinkType::With, in, oldNode, newNode);
    }
    for (Node *in : normInputs) {
        Node::replaceOutput(LinkType::Norm, in, oldNode, newNode);
    }
    for (Node *in : ctrlInputs) {
        Node::replaceOutput(LinkType::Ctrl, in, oldNode, newNode);
    }
    for (Node *out : withOutputs) {
        Node::replaceInput(LinkType::With, out, oldNode, newNode);
    }
    for (Node *out : normOutputs) {
        Node::replaceInput(LinkType::Norm, out, oldNode, newNode);
    }
    for (Node *out : ctrlOutputs) {
        Node::replaceInput(LinkType::Ctrl, out, oldNode, newNode);
    }
    return oldNode->detach();
}

void detail::NodeMutation::link(LinkType type, Node *from, Node *to) { Node::link(type, from, to); }

bool detail::NodeMutation::unlink(Node *from, Node *to) { return Node::unlink(from, to); }

bool detail::NodeMutation::unlinkCtrl(Node *from, Node *to) { return Node::unlinkCtrl(from, to); }

void detail::NodeMutation::replaceInput(
    LinkType type, Node *owner, Node *oldInput, Node *newInput) {
    Node::replaceInput(type, owner, oldInput, newInput);
}

void detail::NodeMutation::replaceOutput(
    LinkType type, Node *owner, Node *oldOutput, Node *newOutput) {
    Node::replaceOutput(type, owner, oldOutput, newOutput);
}

bool detail::NodeMutation::replace(Node *oldNode, Node *newNode) {
    return Node::replace(oldNode, newNode);
}

bool detail::NodeMutation::replaceUses(Node *oldNode, Node *newNode) {
    return Node::replaceUses(oldNode, newNode);
}

void detail::NodeMutation::setBodyGraph(FuncNode *node, Graph *bodyGraph) {
    ASSERT(node != nullptr, "FuncNode is null.");
    node->setBodyGraph(bodyGraph);
}

bool Node::replaceUses(Node *oldNode, Node *newNode) {
    assertDraftMutable(oldNode, "replace node uses");
    assertDraftMutable(newNode, "replace node uses");
    ASSERT(oldNode && newNode, "Cannot replace null nodes.");
    ASSERT(oldNode != newNode, "Cannot replace a node with itself.");
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "GIR",
        "Replacing node uses: {} -> {}",
        oldNode->toString(),
        newNode->toString()));

    const node_vec_t withOutputs(oldNode->withOutputs().begin(), oldNode->withOutputs().end());
    const node_vec_t normOutputs(oldNode->normOutputs().begin(), oldNode->normOutputs().end());
    const node_vec_t ctrlOutputs(oldNode->ctrlOutputs().begin(), oldNode->ctrlOutputs().end());

    for (Node *out : withOutputs) {
        Node::replaceInput(LinkType::With, out, oldNode, newNode);
    }
    for (Node *out : normOutputs) {
        Node::replaceInput(LinkType::Norm, out, oldNode, newNode);
    }
    for (Node *out : ctrlOutputs) {
        Node::replaceInput(LinkType::Ctrl, out, oldNode, newNode);
    }
    return oldNode->detach();
}

bool Node::detach() {
    assertDraftMutable(this, "detach");
    node_vec_t tempWithInputs(withInputs().begin(), withInputs().end());
    for (auto *input : tempWithInputs) {
        if (!unlink(input, this))
            return false;
    }
    node_vec_t tempNormInputs(normInputs().begin(), normInputs().end());
    for (auto *input : tempNormInputs) {
        if (!unlink(input, this))
            return false;
    }
    node_vec_t tempCtrlInputs(ctrlInputs().begin(), ctrlInputs().end());
    for (auto *input : tempCtrlInputs) {
        if (!unlink(input, this))
            return false;
    }
    node_vec_t tempWithOutputs(withOutputs().begin(), withOutputs().end());
    for (auto *output : tempWithOutputs) {
        if (!unlink(this, output))
            return false;
    }
    node_vec_t tempNormOutputs(normOutputs().begin(), normOutputs().end());
    for (auto *output : tempNormOutputs) {
        if (!unlink(this, output))
            return false;
    }
    node_vec_t tempCtrlOutputs(ctrlOutputs().begin(), ctrlOutputs().end());
    for (auto *output : tempCtrlOutputs) {
        if (!unlink(this, output))
            return false;
    }
    return true;
}

// =============================================================================
// DataNode
// =============================================================================

Node *DataNode::create(Graph &graph, const data_ptr_t &data) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addStaticData(data);
    Node *node       = makeOwnedNode<DataNode>(graph, graph, data->type(), index);
    builder.addNode(node);
    return node;
}

Node *DataNode::createStaticSlot(Graph &graph, Type *type, slot_t slot) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addStaticSlot(slot);
    Node *node       = makeOwnedNode<DataNode>(graph, graph, type, index);
    builder.addNode(node);
    return node;
}

std::string DataNode::toString() const {
    std::ostringstream oss;
    camel::core::rtdata::printSlot(oss, dataSlot(), dataType());
    return std::format("DATA({}, {}): {}", dataIndex_, oss.str(), dataType()->toString());
}

Node *DataNode::clone(Graph &graph) const {
    return DataNode::createStaticSlot(graph, dataType(), dataSlot());
}

// =============================================================================
// PortNode
// =============================================================================

Node *PortNode::create(Graph &graph, Type *type, const std::string &name, bool isVar) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    return makeOwnedNode<PortNode>(graph, graph, type, index, name, isVar);
}

std::string PortNode::toString() const {
    return std::format(
        "PORT({}, {}{}): {}",
        dataIndex_,
        isVar_ ? "var " : "",
        name(),
        dataType()->toString());
}

Node *PortNode::clone(Graph &graph) const {
    return PortNode::create(graph, dataType_, name(), isVar_);
}

// =============================================================================
// CastNode, CopyNode, FillNode
// =============================================================================

Node *CastNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<CastNode>(graph, graph, type, index);
    builder.addNode(node);
    return node;
}

std::string CastNode::toString() const {
    return std::format("CAST({}): {}", dataIndex_, dataType()->toString());
}

Node *CastNode::clone(Graph &graph) const { return CastNode::create(graph, dataType_); }

Node *CopyNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<CopyNode>(graph, graph, type, index);
    builder.addNode(node);
    return node;
}

std::string CopyNode::toString() const {
    return std::format("COPY({}): {}", dataIndex_, dataType()->toString());
}

Node *CopyNode::clone(Graph &graph) const { return CopyNode::create(graph, dataType_); }

Node *FillNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<FillNode>(graph, graph, type, index);
    builder.addNode(node);
    return node;
}

std::string FillNode::toString() const {
    return std::format("FILL({}): {}", dataIndex_, dataType()->toString());
}

Node *FillNode::clone(Graph &graph) const { return FillNode::create(graph, dataType_); }

// =============================================================================
// AccsNode
// =============================================================================

Node *AccsNode::create(Graph &graph, Type *type, const std::variant<std::string, size_t> &accsIdx) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node;
    if (std::holds_alternative<size_t>(accsIdx)) {
        node = makeOwnedNode<AccsNode>(graph, graph, type, index, std::get<size_t>(accsIdx));
    } else {
        node = makeOwnedNode<AccsNode>(graph, graph, type, index, std::get<std::string>(accsIdx));
    }
    builder.addNode(node);
    return node;
}

std::string AccsNode::index2String() const {
    return isNum_ ? std::to_string(numIndex_) : strIndex();
}

std::string AccsNode::toString() const {
    return std::format("ACCS({}, ${}): {}", dataIndex_, index2String(), dataType()->toString());
}

Node *AccsNode::clone(Graph &graph) const {
    if (isNum_) {
        return AccsNode::create(graph, dataType_, std::variant<std::string, size_t>(numIndex_));
    }
    return AccsNode::create(graph, dataType_, std::variant<std::string, size_t>(strIndex()));
}

// =============================================================================
// BrchNode, JoinNode, CallNode, BindNode
// =============================================================================

Node *BrchNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<BrchNode>(graph, graph, type, index);
    builder.addNode(node);
    return node;
}

std::string BrchNode::toString() const {
    return std::format("BRCH({}): {}", dataIndex_, dataType()->toString());
}

JoinNode *BrchNode::matchedJoin() const {
    auto no = normOutputs();
    ASSERT(no.size() == 1, "BRCH node must have exactly one matched JOIN output.");
    ASSERT(no.front()->type() == NodeType::JOIN, "BRCH norm output must be JOIN.");
    return tt::as_ptr<JoinNode>(no.front());
}

Node *BrchNode::clone(Graph &graph) const { return BrchNode::create(graph, dataType_); }

Node *JoinNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<JoinNode>(graph, graph, type, index);
    builder.addNode(node);
    return node;
}

std::string JoinNode::toString() const {
    return std::format("JOIN({}): {}", dataIndex_, dataType()->toString());
}

BrchNode *JoinNode::matchedBranch() const {
    auto ni = normInputs();
    ASSERT(!ni.empty(), "JOIN node must have a matched BRCH input.");
    ASSERT(ni.front()->type() == NodeType::BRCH, "JOIN norm input must be BRCH.");
    return tt::as_ptr<BrchNode>(ni.front());
}

Node *JoinNode::clone(Graph &graph) const { return JoinNode::create(graph, dataType_); }

Node *CallNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<CallNode>(graph, graph, type, index);
    builder.addNode(node);
    return node;
}

std::string CallNode::toString() const {
    return std::format("CALL({}): {}", dataIndex_, dataType()->toString());
}

Node *CallNode::clone(Graph &graph) const { return CallNode::create(graph, dataType_); }

Node *BindNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<BindNode>(graph, graph, type, index);
    builder.addNode(node);
    return node;
}

std::string BindNode::toString() const {
    return std::format("BIND({}): {}", dataIndex_, dataType()->toString());
}

Node *BindNode::clone(Graph &graph) const { return BindNode::create(graph, dataType_); }

// =============================================================================
// FuncNode, OperNode
// =============================================================================

Node *FuncNode::create(Graph &graph, const graph_ptr_t &bodyGraph) {
    ASSERT(bodyGraph != nullptr, "Function graph is null for FunctionNode.");
    auto *rtFunc = ::Function::create(
        bodyGraph.get(),
        currentClosureTupleType(bodyGraph.get()),
        graph.arena()->allocator());
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<FuncNode>(graph, graph, index, bodyGraph.get(), rtFunc);
    builder.addNode(node);
    return node;
}

Node *FuncNode::create(Graph &graph, Graph *bodyGraph) {
    ASSERT(bodyGraph != nullptr, "Function graph is null for FunctionNode.");
    auto *rtFunc = ::Function::create(
        bodyGraph,
        currentClosureTupleType(bodyGraph),
        graph.arena()->allocator());
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<FuncNode>(graph, graph, index, bodyGraph, rtFunc);
    builder.addNode(node);
    return node;
}

FunctionType *FuncNode::funcType() const {
    ASSERT(graph_, "Function graph is not set for FunctionNode.");
    return graph_->funcType();
}

JoinNode *FuncNode::matchedJoin() const {
    ASSERT(hasMatchedJoin(), "FUNC node does not have a single matched JOIN continuation.");
    return tt::as_ptr<JoinNode>(withOutputs().front());
}

std::string FuncNode::toString() const {
    return std::format(
        "FUNC({}, {}: {}): {}",
        dataIndex_,
        graph_ ? graph_->name() : "<null>",
        funcType()->toString(),
        dataType()->toString());
}

Node *FuncNode::clone(Graph &graph) const { return FuncNode::create(graph, graph_); }

Node *OperNode::create(Graph &graph, oper_idx_ptr_t op) {
    auto *raw = graph.registerOperIndex(op);
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = makeOwnedNode<OperNode>(graph, graph, index, raw);
    builder.addNode(node);
    return node;
}

FunctionType *OperNode::funcType() const {
    ASSERT(operator_, "Operator is not set for OperatorNode.");
    return tt::as_ptr<FunctionType>(operator_->funcType());
}

std::string OperNode::toString() const {
    return std::format(
        "OPER({}, <{}>, {}: {}): {}",
        dataIndex_,
        operator_->name(),
        operator_->uri(),
        operator_->funcType()->toString(),
        dataType()->toString());
}

Node *OperNode::clone(Graph &graph) const {
    auto sp = graph_->lookupOperIndex(operator_);
    return OperNode::create(graph, sp);
}

// =============================================================================
// DrefNode, SyncNode, GateNode
// =============================================================================

Node *DrefNode::create(Graph &graph, const dref_target_t &target) {
    return makeOwnedNode<DrefNode>(graph, graph, target);
}

std::string DrefNode::toString() const {
    return std::format("DREF({}): {}", graph_->name(), dataType()->toString());
}

Node *DrefNode::clone(Graph &graph) const {
    ASSERT(false, "DrefNode cannot be cloned.");
    return nullptr;
}

Node *SyncNode::create(Graph &graph) {
    GraphBuilder builder(graph);
    Node *node = makeOwnedNode<SyncNode>(graph, graph);
    builder.addNode(node);
    return node;
}

std::string SyncNode::toString() const {
    return std::format("SYNC({}): {}", dataIndex_, dataType()->toString());
}

Node *SyncNode::clone(Graph &graph) const { return SyncNode::create(graph); }

Node *GateNode::create(Graph &graph) {
    GraphBuilder builder(graph);
    Node *node = makeOwnedNode<GateNode>(graph, graph);
    builder.addNode(node);
    return node;
}

std::string GateNode::toString() const {
    return std::format("GATE({}): {}", dataIndex_, dataType()->toString());
}

Node *GateNode::clone(Graph &graph) const {
    Node *node = GateNode::create(graph);
    detail::NodeMutation::setDataType(node, dataType());
    return node;
}

} // namespace camel::compile::gir
