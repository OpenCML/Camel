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
 * Created: Aug. 13, 2024
 * Updated: Mar. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "graph.h"

#include <memory>
#include <unordered_map>
#include <variant>

namespace GraphIR {

// =============================================================================
// Node：图节点基类，持有类型、数据槽索引、入边/出边
// =============================================================================

class Node {
  public:
    Node(Graph &graph, NodeType nodeType, Type *dataType, data_idx_t index)
        : graph_(graph), nodeType_(nodeType), dataType_(dataType), dataIndex_(index) {}
    virtual ~Node() = default;

    NodeType type() const { return nodeType_; }
    Type *dataType() const {
        ASSERT(dataType_ != nullptr, "Node has no data type.");
        return dataType_;
    }
    void setDataType(Type *type) { dataType_ = type; }
    virtual std::string toString() const {
        return std::format("Node({}, {})", to_string(nodeType_), std::to_string(dataIndex_));
    }
    virtual operator std::string() const { return toString(); }
    virtual Node *clone(Graph &graph) const = 0;

    bool operator==(const Node &other) const { return this == &other; }
    bool operator!=(const Node &other) const { return !(this == &other); }

    Graph &graph() const { return graph_; }
    data_idx_t index() const;
    void setIndex(data_idx_t index) { dataIndex_ = index; }
    std::string stableId() const {
        return graph().stableId() + "_n" + std::to_string(static_cast<int>(index()));
    }
    bool macro() const { return macro_; }
    bool constant() const { return const_; }
    void setMacro(bool m) { macro_ = m; }
    void setConstant(bool c) { const_ = c; }

    node_vec_t dataInputs() const {
        node_vec_t inputs;
        inputs.insert(inputs.end(), normInputs_.begin(), normInputs_.end());
        inputs.insert(inputs.end(), withInputs_.begin(), withInputs_.end());
        return inputs;
    }
    node_vec_t &normInputs() { return normInputs_; }
    node_vec_t &withInputs() { return withInputs_; }
    node_vec_t &ctrlInputs() { return ctrlInputs_; }
    node_vec_t inputs() {
        node_vec_t inputs;
        inputs.reserve(normInputs_.size() + withInputs_.size() + ctrlInputs_.size());
        inputs.insert(inputs.end(), normInputs_.begin(), normInputs_.end());
        inputs.insert(inputs.end(), withInputs_.begin(), withInputs_.end());
        inputs.insert(inputs.end(), ctrlInputs_.begin(), ctrlInputs_.end());
        return inputs;
    }

    node_vec_t &normOutputs() { return normOutputs_; }
    node_vec_t &withOutputs() { return withOutputs_; }
    node_vec_t &ctrlOutputs() { return ctrlOutputs_; }
    node_vec_t dataOutputs() {
        node_vec_t outputs;
        outputs.reserve(normOutputs_.size() + withOutputs_.size());
        outputs.insert(outputs.end(), normOutputs_.begin(), normOutputs_.end());
        outputs.insert(outputs.end(), withOutputs_.begin(), withOutputs_.end());
        return outputs;
    }
    node_vec_t outputs() {
        node_vec_t outputs;
        outputs.reserve(normOutputs_.size() + withOutputs_.size() + ctrlOutputs_.size());
        outputs.insert(outputs.end(), normOutputs_.begin(), normOutputs_.end());
        outputs.insert(outputs.end(), withOutputs_.begin(), withOutputs_.end());
        outputs.insert(outputs.end(), ctrlOutputs_.begin(), ctrlOutputs_.end());
        return outputs;
    }

    bool hasDeepLinkedTo(Node *node, size_t maxJumps = 99) const;
    bool hasLinkedTo(Node *node) const;

    size_t inDegree() const { return normInputs_.size() + withInputs_.size() + ctrlInputs_.size(); }
    size_t outDegree() const {
        return withOutputs_.size() + normOutputs_.size() + ctrlOutputs_.size();
    }
    size_t dataInDegree() const { return normInputs_.size() + withInputs_.size(); }
    size_t dataOutDegree() const { return normOutputs_.size() + withOutputs_.size(); }

    bool isSource() const { return inDegree() == 0; }
    bool isReturn() const { return outDegree() == 0; }

    bool detach();

    static void link(LinkType type, Node *from, Node *to);
    static bool unlink(Node *from, Node *to);
    static bool replace(Node *oldNode, Node *newNode);

  protected:
    bool macro_ = false;
    bool const_ = false;

    Graph &graph_;
    NodeType nodeType_;
    Type *dataType_;
    data_idx_t dataIndex_;

    node_vec_t normInputs_;
    node_vec_t withInputs_;
    node_vec_t ctrlInputs_;

    node_vec_t normOutputs_;
    node_vec_t withOutputs_;
    node_vec_t ctrlOutputs_;
};

// 数据与端口类节点
class DataNode : public Node {
  public:
    DataNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::DATA, type, index) {}
    ~DataNode() = default;

    static Node *create(Graph &graph, const data_ptr_t &data) {
        data_idx_t index = graph.addStaticData(data);
        Node *node       = graph.ownNode(std::make_unique<DataNode>(graph, data->type(), index));
        graph.addNode(node);
        return node;
    }

    data_ptr_t data() const { return graph_.getStaticData(dataIndex_); }

    virtual std::string toString() const override {
        return std::format(
            "DATA({}, {}): {}",
            dataIndex_,
            data()->toString(),
            dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return DataNode::create(graph, data()); }
};

class PortNode : public Node {
    std::string name_;
    bool isVar_;

  public:
    PortNode(Graph &graph, Type *type, data_idx_t index, const std::string &name, bool isVar)
        : Node(graph, NodeType::PORT, type, index), name_(name), isVar_(isVar) {}
    ~PortNode() = default;

    static Node *create(Graph &graph, Type *type, const std::string &name, bool isVar) {
        data_idx_t index = graph.addRuntimeData();
        return graph.ownNode(std::make_unique<PortNode>(graph, type, index, name, isVar));
    }

    const std::string &name() const { return name_; }
    bool isVar() const { return isVar_; }

    virtual std::string toString() const override {
        return std::format(
            "PORT({}, {}{}): {}",
            dataIndex_,
            isVar_ ? "var " : "",
            name_,
            dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override {
        return PortNode::create(graph, dataType_, name_, isVar_);
    }
};

class CastNode : public Node {
  public:
    CastNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::CAST, type, index) {}
    ~CastNode() = default;

    static Node *create(Graph &graph, Type *type) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<CastNode>(graph, type, index));
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("CAST({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return CastNode::create(graph, dataType_); }
};

class CopyNode : public Node {
  public:
    CopyNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::COPY, type, index) {}
    ~CopyNode() = default;

    static Node *create(Graph &graph, Type *type) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<CopyNode>(graph, type, index));
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("COPY({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return CopyNode::create(graph, dataType_); }
};

class FillNode : public Node {
  public:
    FillNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::FILL, type, index) {}
    ~FillNode() = default;

    static Node *create(Graph &graph, Type *type) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<FillNode>(graph, type, index));
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("FILL({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return FillNode::create(graph, dataType_); }
};

class AccsNode : public Node {
  public:
    AccsNode(
        Graph &graph, Type *type, data_idx_t index,
        const std::variant<std::string, size_t> &accsIdx)
        : Node(graph, NodeType::ACCS, type, index), accsIndex_(accsIdx) {}
    ~AccsNode() = default;

    static Node *
    create(Graph &graph, Type *type, const std::variant<std::string, size_t> &accsIdx) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<AccsNode>(graph, type, index, accsIdx));
        graph.addNode(node);
        return node;
    }

    bool isNum() const { return std::holds_alternative<size_t>(accsIndex_); }
    template <typename T> T index() const { return std::get<T>(accsIndex_); }
    std::string index2String() const {
        if (std::holds_alternative<size_t>(accsIndex_)) {
            return std::to_string(std::get<size_t>(accsIndex_));
        } else {
            return std::get<std::string>(accsIndex_);
        }
    }

    virtual std::string toString() const override {
        return std::format("ACCS({}, ${}): {}", dataIndex_, index2String(), dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override {
        return AccsNode::create(graph, dataType_, accsIndex_);
    }

  private:
    std::variant<std::string, size_t> accsIndex_;
};

// 控制流节点
class BrchNode : public Node {
  public:
    BrchNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::BRCH, type, index) {}
    ~BrchNode() = default;

    static Node *create(Graph &graph, Type *type) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<BrchNode>(graph, type, index));
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("BRCH({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return BrchNode::create(graph, dataType_); }
};

class JoinNode : public Node {
  public:
    JoinNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::JOIN, type, index) {}
    ~JoinNode() = default;

    static Node *create(Graph &graph, Type *type) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<JoinNode>(graph, type, index));
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("JOIN({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return JoinNode::create(graph, dataType_); }
};

// 调用与函数/算子节点
class CallNode : public Node {
  public:
    CallNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::CALL, type, index) {}
    ~CallNode() = default;

    static Node *create(Graph &graph, Type *type) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<CallNode>(graph, type, index));
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("CALL({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return CallNode::create(graph, dataType_); }
};

class BindNode : public Node {
  public:
    BindNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::BIND, type, index) {}
    ~BindNode() = default;

    static Node *create(Graph &graph, Type *type) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<BindNode>(graph, type, index));
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("BIND({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return BindNode::create(graph, dataType_); }
};

class FuncNode : public Node {
    func_ptr_t func_;
    Graph *graph_; // 构造时确定，FunctionData 必然有 graph

  public:
    FuncNode(Graph &graph, data_idx_t index, func_ptr_t func)
        : Node(graph, NodeType::FUNC, func->funcType()->exitType(), index), func_(func),
          graph_(&func_->graph()) {}
    ~FuncNode() = default;

    static Node *create(Graph &graph, func_ptr_t func) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<FuncNode>(graph, index, func));
        graph.addNode(node);
        return node;
    }

    func_ptr_t func() const { return func_; }
    /** 执行热路径用：构造时已缓存，直接返回，避免返回 func_ptr_t 带来的引用计数开销 */
    Graph *graph() const { return graph_; }
    FunctionType *funcType() const {
        ASSERT(func_, "Function is not set for FunctionNode.");
        return tt::as_ptr<FunctionType>(func_->type());
    }

    virtual std::string toString() const override {
        return std::format(
            "FUNC({}, {}: {}): {}",
            dataIndex_,
            func_->name().empty() ? func_->graph().name() : func_->name(),
            funcType()->toString(),
            dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return FuncNode::create(graph, func_); }
};

class OperNode : public Node {
    oper_idx_ptr_t operator_;
    mutable ::operator_t cachedOp_ = nullptr;

  public:
    OperNode(Graph &graph, data_idx_t index, oper_idx_ptr_t op)
        : Node(graph, NodeType::OPER, op->funcType()->exitType(), index), operator_(op) {}
    ~OperNode() = default;

    static Node *create(Graph &graph, oper_idx_ptr_t op) {
        data_idx_t index = graph.addRuntimeData();
        Node *node       = graph.ownNode(std::make_unique<OperNode>(graph, index, op));
        graph.addNode(node);
        return node;
    }

    oper_idx_ptr_t oper() const { return operator_; }
    FunctionType *funcType() const {
        ASSERT(operator_, "Operator is not set for OperatorNode.");
        return tt::as_ptr<FunctionType>(operator_->funcType());
    }

    ::operator_t getCachedOp() const { return cachedOp_; }
    void setCachedOp(::operator_t op) const { cachedOp_ = op; }

    virtual std::string toString() const override {
        return std::format(
            "OPER({}, <{}>, {}: {}): {}",
            dataIndex_,
            operator_->name(),
            operator_->uri(),
            operator_->funcType()->toString(),
            dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return OperNode::create(graph, operator_); }
};

// 出口与辅助节点
class ExitNode : public Node {
  public:
    ExitNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::EXIT, type, index) {}
    ~ExitNode() = default;

    static Node *create(Graph &graph, Type *type, data_idx_t index = 0) {
        return graph.ownNode(std::make_unique<ExitNode>(graph, type, index));
    }

    virtual std::string toString() const override {
        return std::format("EXIT({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return ExitNode::create(graph, dataType_); }
};

class DrefNode : public Node {
  public:
    using dref_target_t = std::variant<graph_vec_ptr_t, oper_group_ptr_t>;

    DrefNode(Graph &graph, const dref_target_t &target)
        : Node(graph, NodeType::DREF, Type::Void(), 0), target_(target) {}
    ~DrefNode() = default;

    static Node *create(Graph &graph, const dref_target_t &target) {
        return graph.ownNode(std::make_unique<DrefNode>(graph, target));
    }

    const dref_target_t &target() const { return target_; }

    virtual std::string toString() const override {
        return std::format("DREF({}): {}", graph_.name(), dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override {
        ASSERT(false, "DrefNode cannot be cloned.");
        return nullptr;
    }

  private:
    dref_target_t target_;
};

class SyncNode : public Node {
  public:
    SyncNode(Graph &graph) : Node(graph, NodeType::SYNC, Type::Void(), 0) {}
    ~SyncNode() = default;

    static Node *create(Graph &graph) {
        Node *node = graph.ownNode(std::make_unique<SyncNode>(graph));
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("SYNC({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return SyncNode::create(graph); }
};

class NRefNode : public Node {
  public:
    NRefNode(Graph &graph) : Node(graph, NodeType::NREF, Type::Void(), 0) {}
    ~NRefNode() = default;

    static Node *create(Graph &graph) {
        Node *node = graph.ownNode(std::make_unique<NRefNode>(graph));
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override {
        return std::format("NREF({}): {}", dataIndex_, dataType()->toString());
    }

    virtual Node *clone(Graph &graph) const override { return NRefNode::create(graph); }
};

} // namespace GraphIR
