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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "graph.h"

#include <memory>
#include <variant>

namespace camel::compile::gir {

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
    virtual std::string toString() const;
    virtual operator std::string() const;
    virtual Node *clone(Graph &graph) const = 0;

    bool operator==(const Node &other) const { return this == &other; }
    bool operator!=(const Node &other) const { return !(this == &other); }

    Graph &graph() const { return graph_; }
    data_idx_t index() const;
    void setIndex(data_idx_t index) { dataIndex_ = index; }
    std::string stableId() const;
    bool macro() const { return macro_; }
    bool constant() const { return const_; }
    void setMacro(bool m) { macro_ = m; }
    void setConstant(bool c) { const_ = c; }

    node_vec_t dataInputs() const;
    node_vec_t &normInputs() { return normInputs_; }
    node_vec_t &withInputs() { return withInputs_; }
    node_vec_t &ctrlInputs() { return ctrlInputs_; }
    node_vec_t inputs() const;

    node_vec_t &normOutputs() { return normOutputs_; }
    node_vec_t &withOutputs() { return withOutputs_; }
    node_vec_t &ctrlOutputs() { return ctrlOutputs_; }
    node_vec_t dataOutputs() const;
    node_vec_t outputs() const;

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

    static Node *create(Graph &graph, const data_ptr_t &data);

    data_ptr_t data() const { return graph_.getStaticData(dataIndex_); }

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class PortNode : public Node {
    std::string name_;
    bool isVar_;

  public:
    PortNode(Graph &graph, Type *type, data_idx_t index, const std::string &name, bool isVar)
        : Node(graph, NodeType::PORT, type, index), name_(name), isVar_(isVar) {}
    ~PortNode() = default;

    static Node *create(Graph &graph, Type *type, const std::string &name, bool isVar);

    const std::string &name() const { return name_; }
    bool isVar() const { return isVar_; }

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class CastNode : public Node {
  public:
    CastNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::CAST, type, index) {}
    ~CastNode() = default;

    static Node *create(Graph &graph, Type *type);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class CopyNode : public Node {
  public:
    CopyNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::COPY, type, index) {}
    ~CopyNode() = default;

    static Node *create(Graph &graph, Type *type);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class FillNode : public Node {
  public:
    FillNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::FILL, type, index) {}
    ~FillNode() = default;

    static Node *create(Graph &graph, Type *type);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class AccsNode : public Node {
  public:
    AccsNode(
        Graph &graph, Type *type, data_idx_t index,
        const std::variant<std::string, size_t> &accsIdx)
        : Node(graph, NodeType::ACCS, type, index), accsIndex_(accsIdx) {}
    ~AccsNode() = default;

    static Node *create(Graph &graph, Type *type, const std::variant<std::string, size_t> &accsIdx);

    bool isNum() const { return std::holds_alternative<size_t>(accsIndex_); }
    template <typename T> T index() const { return std::get<T>(accsIndex_); }
    std::string index2String() const;

    std::string toString() const override;

    Node *clone(Graph &graph) const override;

  private:
    std::variant<std::string, size_t> accsIndex_;
};

// 控制流节点
class BrchNode : public Node {
  public:
    BrchNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::BRCH, type, index) {}
    ~BrchNode() = default;

    static Node *create(Graph &graph, Type *type);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class JoinNode : public Node {
  public:
    JoinNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::JOIN, type, index) {}
    ~JoinNode() = default;

    static Node *create(Graph &graph, Type *type);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

// 调用与函数/算子节点
class CallNode : public Node {
  public:
    CallNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::CALL, type, index) {}
    ~CallNode() = default;

    static Node *create(Graph &graph, Type *type);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class BindNode : public Node {
  public:
    BindNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::BIND, type, index) {}
    ~BindNode() = default;

    static Node *create(Graph &graph, Type *type);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class FuncNode : public Node {
    func_ptr_t func_;
    Graph *graph_; // 构造时确定，FunctionData 必然有 graph

  public:
    FuncNode(Graph &graph, data_idx_t index, func_ptr_t func)
        : Node(graph, NodeType::FUNC, func->funcType()->exitType(), index), func_(func),
          graph_(&func_->graph()) {}
    ~FuncNode() = default;

    static Node *create(Graph &graph, func_ptr_t func);

    func_ptr_t func() const { return func_; }
    /** 执行热路径用：构造时已缓存，直接返回，避免返回 func_ptr_t 带来的引用计数开销 */
    Graph *graph() const { return graph_; }
    FunctionType *funcType() const;

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class OperNode : public Node {
    oper_idx_ptr_t operator_;
    mutable ::operator_t cachedOp_ = nullptr;

  public:
    OperNode(Graph &graph, data_idx_t index, oper_idx_ptr_t op)
        : Node(graph, NodeType::OPER, op->funcType()->exitType(), index), operator_(op) {}
    ~OperNode() = default;

    static Node *create(Graph &graph, oper_idx_ptr_t op);

    oper_idx_ptr_t oper() const { return operator_; }
    FunctionType *funcType() const;

    ::operator_t getCachedOp() const { return cachedOp_; }
    void setCachedOp(::operator_t op) const { cachedOp_ = op; }

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

// 出口与辅助节点
class ExitNode : public Node {
  public:
    ExitNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::EXIT, type, index) {}
    ~ExitNode() = default;

    static Node *create(Graph &graph, Type *type, data_idx_t index = 0);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class DrefNode : public Node {
  public:
    using dref_target_t = std::variant<graph_vec_ptr_t, oper_group_ptr_t>;

    DrefNode(Graph &graph, const dref_target_t &target)
        : Node(graph, NodeType::DREF, Type::Void(), 0), target_(target) {}
    ~DrefNode() = default;

    static Node *create(Graph &graph, const dref_target_t &target);

    const dref_target_t &target() const { return target_; }

    std::string toString() const override;

    Node *clone(Graph &graph) const override;

  private:
    dref_target_t target_;
};

class SyncNode : public Node {
  public:
    SyncNode(Graph &graph) : Node(graph, NodeType::SYNC, Type::Void(), 0) {}
    ~SyncNode() = default;

    static Node *create(Graph &graph);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class NRefNode : public Node {
  public:
    NRefNode(Graph &graph) : Node(graph, NodeType::NREF, Type::Void(), 0) {}
    ~NRefNode() = default;

    static Node *create(Graph &graph);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

} // namespace camel::compile::gir
