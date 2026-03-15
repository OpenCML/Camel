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
 * Updated: Mar. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir/nodes.h"
#include "camel/compile/gir/builder.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"
#include <atomic>
#include <functional>

namespace camel::compile::gir {

namespace {

std::string makeNodeStableId(Graph &graph, NodeType nodeType) {
    // Node index is reassigned by Graph::rearrange(), and graph rewrites such as
    // inlining can also move/clone nodes across graphs. If stableId depended on
    // index(), any previously registered node->origin mapping would become stale.
    // Generate an immutable id once at construction time instead.
    static std::atomic<uint64_t> nextId = 1;
    return std::format("{}::{}#{}", graph.stableId(), to_string(nodeType), nextId++);
}

} // namespace

// =============================================================================
// Node 索引与连通性
// =============================================================================

std::string Node::makeStableId(Graph &graph, NodeType nodeType) {
    return makeNodeStableId(graph, nodeType);
}

data_idx_t Node::index() const {
    ASSERT(nodeType_ != NodeType::SYNC, "SYNC node has no data index.");
    ASSERT(nodeType_ != NodeType::DREF, "DREF node has no data index.");
    if (nodeType_ == NodeType::NREF) {
        return normInputs_.front()->index();
    }
    return dataIndex_;
}

std::string Node::toString() const {
    return std::format("Node({}, {})", to_string(nodeType_), std::to_string(dataIndex_));
}

Node::operator std::string() const { return toString(); }

node_vec_t Node::dataInputs() const {
    node_vec_t inputs;
    inputs.insert(inputs.end(), normInputs_.begin(), normInputs_.end());
    inputs.insert(inputs.end(), withInputs_.begin(), withInputs_.end());
    return inputs;
}

node_vec_t Node::inputs() const {
    node_vec_t inputs;
    inputs.reserve(normInputs_.size() + withInputs_.size() + ctrlInputs_.size());
    inputs.insert(inputs.end(), normInputs_.begin(), normInputs_.end());
    inputs.insert(inputs.end(), withInputs_.begin(), withInputs_.end());
    inputs.insert(inputs.end(), ctrlInputs_.begin(), ctrlInputs_.end());
    return inputs;
}

node_vec_t Node::dataOutputs() const {
    node_vec_t outputs;
    outputs.reserve(normOutputs_.size() + withOutputs_.size());
    outputs.insert(outputs.end(), normOutputs_.begin(), normOutputs_.end());
    outputs.insert(outputs.end(), withOutputs_.begin(), withOutputs_.end());
    return outputs;
}

node_vec_t Node::outputs() const {
    node_vec_t outputs;
    outputs.reserve(normOutputs_.size() + withOutputs_.size() + ctrlOutputs_.size());
    outputs.insert(outputs.end(), normOutputs_.begin(), normOutputs_.end());
    outputs.insert(outputs.end(), withOutputs_.begin(), withOutputs_.end());
    outputs.insert(outputs.end(), ctrlOutputs_.begin(), ctrlOutputs_.end());
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
            EXEC_WHEN_DEBUG(
                GetDefaultLogger().in("GIR").warn(
                    "Deep link check reached max jumps at node: {}.",
                    node->toString()));
            return false;
        }

        visited.insert(current);

        for (auto *out : current->withOutputs_) {
            if (out == node)
                return true;
            if (visited.find(out) == visited.end()) {
                if (dfs(out, jumpsLeft - 1))
                    return true;
            }
        }
        for (auto *out : current->normOutputs_) {
            if (out == node)
                return true;
            if (visited.find(out) == visited.end()) {
                if (dfs(out, jumpsLeft - 1))
                    return true;
            }
        }
        for (auto *out : current->ctrlOutputs_) {
            if (out == node)
                return true;
            if (visited.find(out) == visited.end()) {
                if (dfs(out, jumpsLeft - 1))
                    return true;
            }
        }
        return false;
    };

    for (auto *out : withOutputs_) {
        if (dfs(out, maxJumps - 1))
            return true;
    }
    for (auto *out : normOutputs_) {
        if (dfs(out, maxJumps - 1))
            return true;
    }
    for (auto *out : ctrlOutputs_) {
        if (dfs(out, maxJumps - 1))
            return true;
    }
    return false;
}

bool Node::hasLinkedTo(Node *node) const {
    for (auto *out : withOutputs_) {
        if (out == node)
            return true;
    }
    for (auto *out : normOutputs_) {
        if (out == node)
            return true;
    }
    for (auto *out : ctrlOutputs_) {
        if (out == node)
            return true;
    }
    return false;
}

JoinNode *Node::matchedJoinOutput() const {
    ASSERT(hasMatchedJoinOutput(), "Node does not have a single matched JOIN output.");
    return tt::as_ptr<JoinNode>(withOutputs_.front());
}

// =============================================================================
// Node 连边与替换
// =============================================================================

void Node::link(LinkType type, Node *from, Node *to) {
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

    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
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
    ASSERT(from && to, "Cannot unlink null nodes.");
    ASSERT(from != to, "Cannot unlink a node from itself.");
    ASSERT(
        &from->graph() == &to->graph(),
        std::format(
            "Cannot unlink nodes from different graphs: {} -X- {}.",
            from->toString(),
            to->toString()));

    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Unlinking nodes: {} -X- {}",
            from->toString(),
            to->toString()));

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
        return NodeMutation::withInputs(node);
    case LinkType::Norm:
        return NodeMutation::normInputs(node);
    case LinkType::Ctrl:
        return NodeMutation::ctrlInputs(node);
    }
    ASSERT(false, "Unsupported link type when selecting inputs.");
    return NodeMutation::normInputs(node);
}

node_vec_t &selectOutputs(LinkType type, Node *node) {
    switch (type) {
    case LinkType::With:
        return NodeMutation::withOutputs(node);
    case LinkType::Norm:
        return NodeMutation::normOutputs(node);
    case LinkType::Ctrl:
        return NodeMutation::ctrlOutputs(node);
    }
    ASSERT(false, "Unsupported link type when selecting outputs.");
    return NodeMutation::normOutputs(node);
}

size_t eraseOne(node_vec_t &nodes, Node *target) {
    auto it = std::find(nodes.begin(), nodes.end(), target);
    ASSERT(it != nodes.end(), "Target edge not found when erasing one occurrence.");
    const size_t index = static_cast<size_t>(it - nodes.begin());
    nodes.erase(it);
    return index;
}

} // namespace

void Node::replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput) {
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

    auto existingIt = std::find(inputs.begin(), inputs.end(), newInput);
    if (existingIt != inputs.end()) {
        size_t existingIndex = eraseOne(inputs, newInput);
        eraseOne(newOutputs, owner);
        if (existingIndex < ownerIndex) {
            ownerIndex--;
        }
    }

    inputs[ownerIndex] = newInput;
    eraseOne(oldOutputs, owner);
    newOutputs.push_back(owner);
}

void Node::replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput) {
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

    auto existingIt = std::find(outputs.begin(), outputs.end(), newOutput);
    if (existingIt != outputs.end()) {
        size_t existingIndex = eraseOne(outputs, newOutput);
        eraseOne(newInputs, owner);
        if (existingIndex < ownerIndex) {
            ownerIndex--;
        }
    }

    outputs[ownerIndex] = newOutput;
    eraseOne(oldInputs, owner);
    newInputs.push_back(owner);
}

bool Node::replace(Node *oldNode, Node *newNode) {
    ASSERT(oldNode && newNode, "Cannot replace null nodes.");
    ASSERT(oldNode != newNode, "Cannot replace a node with itself.");
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Replacing node: {} -> {}",
            oldNode->toString(),
            newNode->toString()));

    const node_vec_t withInputs  = oldNode->withInputs_;
    const node_vec_t normInputs  = oldNode->normInputs_;
    const node_vec_t ctrlInputs  = oldNode->ctrlInputs_;
    const node_vec_t withOutputs = oldNode->withOutputs_;
    const node_vec_t normOutputs = oldNode->normOutputs_;
    const node_vec_t ctrlOutputs = oldNode->ctrlOutputs_;

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

void NodeMutation::link(LinkType type, Node *from, Node *to) { Node::link(type, from, to); }

bool NodeMutation::unlink(Node *from, Node *to) { return Node::unlink(from, to); }

bool NodeMutation::unlinkCtrl(Node *from, Node *to) { return Node::unlinkCtrl(from, to); }

void NodeMutation::replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput) {
    Node::replaceInput(type, owner, oldInput, newInput);
}

void NodeMutation::replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput) {
    Node::replaceOutput(type, owner, oldOutput, newOutput);
}

bool NodeMutation::replace(Node *oldNode, Node *newNode) { return Node::replace(oldNode, newNode); }

bool NodeMutation::replaceUses(Node *oldNode, Node *newNode) {
    return Node::replaceUses(oldNode, newNode);
}

bool Node::replaceUses(Node *oldNode, Node *newNode) {
    ASSERT(oldNode && newNode, "Cannot replace null nodes.");
    ASSERT(oldNode != newNode, "Cannot replace a node with itself.");
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Replacing node uses: {} -> {}",
            oldNode->toString(),
            newNode->toString()));

    const node_vec_t withOutputs = oldNode->withOutputs_;
    const node_vec_t normOutputs = oldNode->normOutputs_;
    const node_vec_t ctrlOutputs = oldNode->ctrlOutputs_;

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
    auto tempWithInputs = withInputs_;
    for (auto *input : tempWithInputs) {
        if (!unlink(input, this))
            return false;
    }
    auto tempNormInputs = normInputs_;
    for (auto *input : tempNormInputs) {
        if (!unlink(input, this))
            return false;
    }
    auto tempCtrlInputs = ctrlInputs_;
    for (auto *input : tempCtrlInputs) {
        if (!unlink(input, this))
            return false;
    }
    auto tempWithOutputs = withOutputs_;
    for (auto *output : tempWithOutputs) {
        if (!unlink(this, output))
            return false;
    }
    auto tempNormOutputs = normOutputs_;
    for (auto *output : tempNormOutputs) {
        if (!unlink(this, output))
            return false;
    }
    auto tempCtrlOutputs = ctrlOutputs_;
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
    Node *node       = builder.ownNode(std::make_unique<DataNode>(graph, data->type(), index));
    builder.addNode(node);
    return node;
}

std::string DataNode::toString() const {
    return std::format("DATA({}, {}): {}", dataIndex_, data()->toString(), dataType()->toString());
}

Node *DataNode::clone(Graph &graph) const {
    // Graph clone/rewrite 需要复制原始编译期静态条目，而不是把 frozen static slot 先
    // materialize 回 data_ptr_t 再二次解释。否则某些只允许运行时表示的静态 ref 槽位
    // 会在 clone 阶段被错误展开。
    return DataNode::create(graph, graph_.getStaticData(dataIndex_));
}

// =============================================================================
// PortNode
// =============================================================================

Node *PortNode::create(Graph &graph, Type *type, const std::string &name, bool isVar) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    return builder.ownNode(std::make_unique<PortNode>(graph, type, index, name, isVar));
}

std::string PortNode::toString() const {
    return std::format(
        "PORT({}, {}{}): {}",
        dataIndex_,
        isVar_ ? "var " : "",
        name_,
        dataType()->toString());
}

Node *PortNode::clone(Graph &graph) const {
    return PortNode::create(graph, dataType_, name_, isVar_);
}

// =============================================================================
// CastNode, CopyNode, FillNode
// =============================================================================

Node *CastNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = builder.ownNode(std::make_unique<CastNode>(graph, type, index));
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
    Node *node       = builder.ownNode(std::make_unique<CopyNode>(graph, type, index));
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
    Node *node       = builder.ownNode(std::make_unique<FillNode>(graph, type, index));
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
    Node *node       = builder.ownNode(std::make_unique<AccsNode>(graph, type, index, accsIdx));
    builder.addNode(node);
    return node;
}

std::string AccsNode::index2String() const {
    if (std::holds_alternative<size_t>(accsIndex_)) {
        return std::to_string(std::get<size_t>(accsIndex_));
    }
    return std::get<std::string>(accsIndex_);
}

std::string AccsNode::toString() const {
    return std::format("ACCS({}, ${}): {}", dataIndex_, index2String(), dataType()->toString());
}

Node *AccsNode::clone(Graph &graph) const { return AccsNode::create(graph, dataType_, accsIndex_); }

// =============================================================================
// BrchNode, JoinNode, CallNode, BindNode
// =============================================================================

Node *BrchNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = builder.ownNode(std::make_unique<BrchNode>(graph, type, index));
    builder.addNode(node);
    return node;
}

std::string BrchNode::toString() const {
    return std::format("BRCH({}): {}", dataIndex_, dataType()->toString());
}

JoinNode *BrchNode::matchedJoin() const {
    ASSERT(normOutputs_.size() == 1, "BRCH node must have exactly one matched JOIN output.");
    ASSERT(normOutputs_.front()->type() == NodeType::JOIN, "BRCH norm output must be JOIN.");
    return tt::as_ptr<JoinNode>(normOutputs_.front());
}

Node *BrchNode::clone(Graph &graph) const { return BrchNode::create(graph, dataType_); }

Node *JoinNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = builder.ownNode(std::make_unique<JoinNode>(graph, type, index));
    builder.addNode(node);
    return node;
}

std::string JoinNode::toString() const {
    return std::format("JOIN({}): {}", dataIndex_, dataType()->toString());
}

BrchNode *JoinNode::matchedBranch() const {
    ASSERT(!normInputs_.empty(), "JOIN node must have a matched BRCH input.");
    ASSERT(normInputs_.front()->type() == NodeType::BRCH, "JOIN norm input must be BRCH.");
    return tt::as_ptr<BrchNode>(normInputs_.front());
}

Node *JoinNode::clone(Graph &graph) const { return JoinNode::create(graph, dataType_); }

Node *CallNode::create(Graph &graph, Type *type) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = builder.ownNode(std::make_unique<CallNode>(graph, type, index));
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
    Node *node       = builder.ownNode(std::make_unique<BindNode>(graph, type, index));
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

Node *FuncNode::create(Graph &graph, func_ptr_t func) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = builder.ownNode(std::make_unique<FuncNode>(graph, index, func));
    builder.addNode(node);
    return node;
}

FunctionType *FuncNode::funcType() const {
    ASSERT(func_, "Function is not set for FunctionNode.");
    return tt::as_ptr<FunctionType>(func_->type());
}

JoinNode *FuncNode::matchedJoin() const {
    ASSERT(hasMatchedJoin(), "FUNC node does not have a single matched JOIN continuation.");
    return tt::as_ptr<JoinNode>(withOutputs_.front());
}

std::string FuncNode::toString() const {
    return std::format(
        "FUNC({}, {}: {}): {}",
        dataIndex_,
        func_->name().empty() ? func_->graph().name() : func_->name(),
        funcType()->toString(),
        dataType()->toString());
}

Node *FuncNode::clone(Graph &graph) const { return FuncNode::create(graph, func_); }

Node *OperNode::create(Graph &graph, oper_idx_ptr_t op) {
    GraphBuilder builder(graph);
    data_idx_t index = builder.addRuntimeData();
    Node *node       = builder.ownNode(std::make_unique<OperNode>(graph, index, op));
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

Node *OperNode::clone(Graph &graph) const { return OperNode::create(graph, operator_); }

// =============================================================================
// ExitNode, DrefNode, SyncNode, NRefNode
// =============================================================================

Node *ExitNode::create(Graph &graph, Type *type, data_idx_t index) {
    return GraphBuilder(graph).ownNode(std::make_unique<ExitNode>(graph, type, index));
}

std::string ExitNode::toString() const {
    return std::format("EXIT({}): {}", dataIndex_, dataType()->toString());
}

Node *ExitNode::clone(Graph &graph) const { return ExitNode::create(graph, dataType_); }

Node *DrefNode::create(Graph &graph, const dref_target_t &target) {
    return GraphBuilder(graph).ownNode(std::make_unique<DrefNode>(graph, target));
}

std::string DrefNode::toString() const {
    return std::format("DREF({}): {}", graph_.name(), dataType()->toString());
}

Node *DrefNode::clone(Graph &graph) const {
    ASSERT(false, "DrefNode cannot be cloned.");
    return nullptr;
}

Node *SyncNode::create(Graph &graph) {
    GraphBuilder builder(graph);
    Node *node = builder.ownNode(std::make_unique<SyncNode>(graph));
    builder.addNode(node);
    return node;
}

std::string SyncNode::toString() const {
    return std::format("SYNC({}): {}", dataIndex_, dataType()->toString());
}

Node *SyncNode::clone(Graph &graph) const { return SyncNode::create(graph); }

Node *NRefNode::create(Graph &graph) {
    GraphBuilder builder(graph);
    Node *node = builder.ownNode(std::make_unique<NRefNode>(graph));
    builder.addNode(node);
    return node;
}

std::string NRefNode::toString() const {
    return std::format("NREF({}): {}", dataIndex_, dataType()->toString());
}

Node *NRefNode::clone(Graph &graph) const { return NRefNode::create(graph); }

} // namespace camel::compile::gir
