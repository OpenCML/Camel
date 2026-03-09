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
 * Updated: Mar. 09, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/compile/gir/nodes.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"
#include <atomic>
#include <functional>

namespace camel::compile::gir {

// =============================================================================
// Node 索引与连通性
// =============================================================================

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

std::string Node::stableId() const {
    static std::atomic<int> cnt_ = 0;
    std::string id               = graph().stableId();
    id += "_" + to_string(nodeType_) + "_";
    if (nodeType_ == NodeType::DREF || nodeType_ == NodeType::SYNC) {
        id += std::to_string(cnt_++);
    } else {
        id += std::to_string(static_cast<int>(index()));
    }
    return id;
}

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

bool Node::replace(Node *oldNode, Node *newNode) {
    ASSERT(oldNode && newNode, "Cannot replace null nodes.");
    ASSERT(oldNode != newNode, "Cannot replace a node with itself.");
    EXEC_WHEN_DEBUG(
        GetDefaultLogger().in("GIR").debug(
            "Replacing node: {} -> {}",
            oldNode->toString(),
            newNode->toString()));

    for (const auto &in : oldNode->withInputs_) {
        Node::link(LinkType::With, in, newNode);
    }
    for (const auto &in : oldNode->normInputs_) {
        Node::link(LinkType::Norm, in, newNode);
    }
    for (const auto &in : oldNode->ctrlInputs_) {
        Node::link(LinkType::Ctrl, in, newNode);
    }
    for (const auto &out : oldNode->withOutputs_) {
        Node::link(LinkType::With, newNode, out);
    }
    for (const auto &out : oldNode->normOutputs_) {
        Node::link(LinkType::Norm, newNode, out);
    }
    for (const auto &out : oldNode->ctrlOutputs_) {
        Node::link(LinkType::Ctrl, newNode, out);
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
    data_idx_t index = graph.addStaticData(data);
    Node *node       = graph.ownNode(std::make_unique<DataNode>(graph, data->type(), index));
    graph.addNode(node);
    return node;
}

std::string DataNode::toString() const {
    return std::format("DATA({}, {}): {}", dataIndex_, data()->toString(), dataType()->toString());
}

Node *DataNode::clone(Graph &graph) const { return DataNode::create(graph, data()); }

// =============================================================================
// PortNode
// =============================================================================

Node *PortNode::create(Graph &graph, Type *type, const std::string &name, bool isVar) {
    data_idx_t index = graph.addRuntimeData();
    return graph.ownNode(std::make_unique<PortNode>(graph, type, index, name, isVar));
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
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<CastNode>(graph, type, index));
    graph.addNode(node);
    return node;
}

std::string CastNode::toString() const {
    return std::format("CAST({}): {}", dataIndex_, dataType()->toString());
}

Node *CastNode::clone(Graph &graph) const { return CastNode::create(graph, dataType_); }

Node *CopyNode::create(Graph &graph, Type *type) {
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<CopyNode>(graph, type, index));
    graph.addNode(node);
    return node;
}

std::string CopyNode::toString() const {
    return std::format("COPY({}): {}", dataIndex_, dataType()->toString());
}

Node *CopyNode::clone(Graph &graph) const { return CopyNode::create(graph, dataType_); }

Node *FillNode::create(Graph &graph, Type *type) {
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<FillNode>(graph, type, index));
    graph.addNode(node);
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
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<AccsNode>(graph, type, index, accsIdx));
    graph.addNode(node);
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
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<BrchNode>(graph, type, index));
    graph.addNode(node);
    return node;
}

std::string BrchNode::toString() const {
    return std::format("BRCH({}): {}", dataIndex_, dataType()->toString());
}

Node *BrchNode::clone(Graph &graph) const { return BrchNode::create(graph, dataType_); }

Node *JoinNode::create(Graph &graph, Type *type) {
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<JoinNode>(graph, type, index));
    graph.addNode(node);
    return node;
}

std::string JoinNode::toString() const {
    return std::format("JOIN({}): {}", dataIndex_, dataType()->toString());
}

Node *JoinNode::clone(Graph &graph) const { return JoinNode::create(graph, dataType_); }

Node *CallNode::create(Graph &graph, Type *type) {
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<CallNode>(graph, type, index));
    graph.addNode(node);
    return node;
}

std::string CallNode::toString() const {
    return std::format("CALL({}): {}", dataIndex_, dataType()->toString());
}

Node *CallNode::clone(Graph &graph) const { return CallNode::create(graph, dataType_); }

Node *BindNode::create(Graph &graph, Type *type) {
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<BindNode>(graph, type, index));
    graph.addNode(node);
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
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<FuncNode>(graph, index, func));
    graph.addNode(node);
    return node;
}

FunctionType *FuncNode::funcType() const {
    ASSERT(func_, "Function is not set for FunctionNode.");
    return tt::as_ptr<FunctionType>(func_->type());
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
    data_idx_t index = graph.addRuntimeData();
    Node *node       = graph.ownNode(std::make_unique<OperNode>(graph, index, op));
    graph.addNode(node);
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
    return graph.ownNode(std::make_unique<ExitNode>(graph, type, index));
}

std::string ExitNode::toString() const {
    return std::format("EXIT({}): {}", dataIndex_, dataType()->toString());
}

Node *ExitNode::clone(Graph &graph) const { return ExitNode::create(graph, dataType_); }

Node *DrefNode::create(Graph &graph, const dref_target_t &target) {
    return graph.ownNode(std::make_unique<DrefNode>(graph, target));
}

std::string DrefNode::toString() const {
    return std::format("DREF({}): {}", graph_.name(), dataType()->toString());
}

Node *DrefNode::clone(Graph &graph) const {
    ASSERT(false, "DrefNode cannot be cloned.");
    return nullptr;
}

Node *SyncNode::create(Graph &graph) {
    Node *node = graph.ownNode(std::make_unique<SyncNode>(graph));
    graph.addNode(node);
    return node;
}

std::string SyncNode::toString() const {
    return std::format("SYNC({}): {}", dataIndex_, dataType()->toString());
}

Node *SyncNode::clone(Graph &graph) const { return SyncNode::create(graph); }

Node *NRefNode::create(Graph &graph) {
    Node *node = graph.ownNode(std::make_unique<NRefNode>(graph));
    graph.addNode(node);
    return node;
}

std::string NRefNode::toString() const {
    return std::format("NREF({}): {}", dataIndex_, dataType()->toString());
}

Node *NRefNode::clone(Graph &graph) const { return NRefNode::create(graph); }

} // namespace camel::compile::gir
