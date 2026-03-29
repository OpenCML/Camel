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
 * Updated: Mar. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/operator.h"
#include "graph.h"

#include <variant>

class Function;

namespace camel::compile::gir {

class GraphArena;
class JoinNode;
class FuncNode;
namespace detail {
class NodeMutation;
}

// =============================================================================
// Node：图节点基类，持有类型、数据槽索引、入边/出边
// =============================================================================

class Node {
  public:
    Node(Graph &graph, NodeType nodeType, Type *dataType, data_idx_t index)
        : graph_(&graph), nodeType_(nodeType), dataType_(dataType), dataIndex_(index) {
        Graph::installProvisionalNodeStableId(graph, this);
    }
    virtual ~Node() = default;

    NodeType type() const { return nodeType_; }
    Type *dataType() const {
        ASSERT(dataType_ != nullptr, "Node has no data type.");
        return dataType_;
    }
    virtual std::string toString() const;
    virtual operator std::string() const;
    virtual Node *clone(Graph &graph) const = 0;

    bool operator==(const Node &other) const { return this == &other; }
    bool operator!=(const Node &other) const { return !(this == &other); }

    Graph &graph() const { return *graph_; }
    data_idx_t index() const;
    const std::string &debugEntityId() const { return graph_->nodeDebugEntityId(this); }
    bool macro() const { return macro_; }
    bool constant() const { return const_; }

    node_vec_t dataInputs() const;
    node_span_t normInputs() const {
        return frozen_ ? node_span_t{frzNormIn_.p, frzNormIn_.n}
                       : node_span_t{normInputs_.data(), normInputs_.size()};
    }
    node_span_t withInputs() const {
        return frozen_ ? node_span_t{frzWithIn_.p, frzWithIn_.n}
                       : node_span_t{withInputs_.data(), withInputs_.size()};
    }
    node_span_t ctrlInputs() const {
        return frozen_ ? node_span_t{frzCtrlIn_.p, frzCtrlIn_.n}
                       : node_span_t{ctrlInputs_.data(), ctrlInputs_.size()};
    }
    node_vec_t inputs() const;

    node_span_t normOutputs() const {
        return frozen_ ? node_span_t{frzNormOut_.p, frzNormOut_.n}
                       : node_span_t{normOutputs_.data(), normOutputs_.size()};
    }
    node_span_t withOutputs() const {
        return frozen_ ? node_span_t{frzWithOut_.p, frzWithOut_.n}
                       : node_span_t{withOutputs_.data(), withOutputs_.size()};
    }
    node_span_t ctrlOutputs() const {
        return frozen_ ? node_span_t{frzCtrlOut_.p, frzCtrlOut_.n}
                       : node_span_t{ctrlOutputs_.data(), ctrlOutputs_.size()};
    }
    node_vec_t dataOutputs() const;
    node_vec_t outputs() const;

    bool hasDeepLinkedTo(Node *node, size_t maxJumps = 99) const;
    bool hasLinkedTo(Node *node) const;
    bool hasMatchedJoinOutput() const {
        auto wo = withOutputs();
        return wo.size() == 1 && wo.front()->type() == NodeType::JOIN;
    }
    JoinNode *matchedJoinOutput() const;

    size_t inDegree() const {
        return normInputs().size() + withInputs().size() + ctrlInputs().size();
    }
    size_t outDegree() const {
        return withOutputs().size() + normOutputs().size() + ctrlOutputs().size();
    }
    size_t dataInDegree() const { return normInputs().size() + withInputs().size(); }
    size_t dataOutDegree() const { return normOutputs().size() + withOutputs().size(); }
    bool isFrozen() const { return frozen_; }

    bool isSource() const { return inDegree() == 0; }
    bool isReturn() const { return outDegree() == 0; }

    bool detach();

  protected:
    void setDataType(Type *type) {
        ASSERT(!graph_->finalized(), "Cannot mutate node type after graph is sealed.");
        dataType_ = type;
    }
    void setIndex(data_idx_t index) {
        ASSERT(!graph_->finalized(), "Cannot mutate node index after graph is sealed.");
        dataIndex_ = index;
    }
    void setMacro(bool m) {
        ASSERT(!graph_->finalized(), "Cannot mutate node flags after graph is sealed.");
        macro_ = m;
    }
    void setConstant(bool c) {
        ASSERT(!graph_->finalized(), "Cannot mutate node flags after graph is sealed.");
        const_ = c;
    }

    bool macro_  = false;
    bool const_  = false;
    bool frozen_ = false;

    Graph *graph_;
    NodeType nodeType_;
    Type *dataType_;
    data_idx_t dataIndex_;

    // --- Draft 模式邻接表 ---
    // 仅 draft 节点（frozen_==false）使用，支持动态增删。
    node_vec_t normInputs_;
    node_vec_t withInputs_;
    node_vec_t ctrlInputs_;

    node_vec_t normOutputs_;
    node_vec_t withOutputs_;
    node_vec_t ctrlOutputs_;

    // --- Frozen 模式邻接表 ---
    // 仅 frozen 节点（frozen_==true）使用，指向 arena 上的定长数组。
    // Frozen 节点的 draft vectors 保持空（默认构造态），不占 heap 内存。
    struct FrzAdj {
        Node **p   = nullptr;
        uint16_t n = 0;
    };
    FrzAdj frzNormIn_, frzWithIn_, frzCtrlIn_;
    FrzAdj frzNormOut_, frzWithOut_, frzCtrlOut_;

  private:
    friend class Builder;
    friend class GraphBuilder;
    friend class GraphRewriteSession;
    friend class detail::NodeMutation;

    node_vec_t &mutableNormInputs() {
        ASSERT(!frozen_, "Cannot mutate frozen node.");
        return normInputs_;
    }
    node_vec_t &mutableWithInputs() {
        ASSERT(!frozen_, "Cannot mutate frozen node.");
        return withInputs_;
    }
    node_vec_t &mutableCtrlInputs() {
        ASSERT(!frozen_, "Cannot mutate frozen node.");
        return ctrlInputs_;
    }
    node_vec_t &mutableNormOutputs() {
        ASSERT(!frozen_, "Cannot mutate frozen node.");
        return normOutputs_;
    }
    node_vec_t &mutableWithOutputs() {
        ASSERT(!frozen_, "Cannot mutate frozen node.");
        return withOutputs_;
    }
    node_vec_t &mutableCtrlOutputs() {
        ASSERT(!frozen_, "Cannot mutate frozen node.");
        return ctrlOutputs_;
    }

    /// 将 draft 邻接 vectors 搬迁到 arena 定长数组，然后清空 vectors 并置 frozen_=true。
    /// 由 GraphBuilder::sealGraph() 在 freeze 阶段调用。
    void freezeAdjacency(GraphArena &arena);

    static void link(LinkType type, Node *from, Node *to);
    static bool unlink(Node *from, Node *to);
    static bool unlinkCtrl(Node *from, Node *to);
    static void replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput);
    static void replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput);
    static bool replace(Node *oldNode, Node *newNode);
    static bool replaceUses(Node *oldNode, Node *newNode);
};

namespace detail {
// 内部节点改写桥接：仅供 GraphBuilder/GraphDraft/rewrite 通道使用，
// 不作为对外稳定 API。
class NodeMutation {
  public:
    static void setDataType(Node *node, Type *type) { node->setDataType(type); }
    static void setIndex(Node *node, data_idx_t index) { node->setIndex(index); }
    static void setMacro(Node *node, bool m) { node->setMacro(m); }
    static void setConstant(Node *node, bool c) { node->setConstant(c); }
    static void setBodyGraph(FuncNode *node, Graph *bodyGraph);

    static node_vec_t &normInputs(Node *node) { return node->mutableNormInputs(); }
    static node_vec_t &withInputs(Node *node) { return node->mutableWithInputs(); }
    static node_vec_t &ctrlInputs(Node *node) { return node->mutableCtrlInputs(); }
    static node_vec_t &normOutputs(Node *node) { return node->mutableNormOutputs(); }
    static node_vec_t &withOutputs(Node *node) { return node->mutableWithOutputs(); }
    static node_vec_t &ctrlOutputs(Node *node) { return node->mutableCtrlOutputs(); }

    static void link(LinkType type, Node *from, Node *to);
    static bool unlink(Node *from, Node *to);
    static bool unlinkCtrl(Node *from, Node *to);
    static void replaceInput(LinkType type, Node *owner, Node *oldInput, Node *newInput);
    static void replaceOutput(LinkType type, Node *owner, Node *oldOutput, Node *newOutput);
    static bool replace(Node *oldNode, Node *newNode);
    static bool replaceUses(Node *oldNode, Node *newNode);
};
} // namespace detail

// 数据与端口类节点
class DataNode : public Node {
  public:
    DataNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::DATA, type, index) {}
    ~DataNode() = default;

    static Node *create(Graph &graph, const data_ptr_t &data);
    static Node *createStaticSlot(Graph &graph, Type *type, slot_t slot);

    slot_t dataSlot() const { return graph_->getStaticDataSlot(dataIndex_); }

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class PortNode : public Node {
    bool isVar_;

  public:
    PortNode(Graph &graph, Type *type, data_idx_t index, const std::string &name, bool isVar)
        : Node(graph, NodeType::PORT, type, index), isVar_(isVar) {
        graph.nodePortNames_[this] = name;
    }
    ~PortNode() = default;

    static Node *create(Graph &graph, Type *type, const std::string &name, bool isVar);

    const std::string &name() const { return graph_->nodePortName(this); }
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
    AccsNode(Graph &graph, Type *type, data_idx_t index, size_t numIdx)
        : Node(graph, NodeType::ACCS, type, index), isNum_(true), numIndex_(numIdx) {}
    AccsNode(Graph &graph, Type *type, data_idx_t index, const std::string &strKey)
        : Node(graph, NodeType::ACCS, type, index), isNum_(false), numIndex_(0) {
        graph.nodeAccsKeys_[this] = strKey;
    }
    ~AccsNode() = default;

    static Node *create(Graph &graph, Type *type, const std::variant<std::string, size_t> &accsIdx);

    bool isNum() const { return isNum_; }
    size_t numIndex() const {
        ASSERT(isNum_, "AccsNode: numeric index requested on string-keyed accessor.");
        return numIndex_;
    }
    const std::string &strIndex() const {
        ASSERT(!isNum_, "AccsNode: string index requested on numeric accessor.");
        return graph_->nodeAccsKey(this);
    }
    std::string index2String() const;

    std::string toString() const override;

    Node *clone(Graph &graph) const override;

  private:
    bool isNum_;
    size_t numIndex_;
};

// 控制流节点
class BrchNode : public Node {
  public:
    BrchNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::BRCH, type, index) {}
    ~BrchNode() = default;

    static Node *create(Graph &graph, Type *type);

    bool hasSelectorInput() const { return normInputs().size() == 1; }
    Node *selectorInput() const {
        ASSERT(normInputs().size() == 1, "BRCH node must have exactly one selector input.");
        return normInputs().front();
    }
    node_span_t caseInputs() const { return withInputs(); }
    bool hasMatchedJoin() const {
        auto no = normOutputs();
        return no.size() == 1 && no.front()->type() == NodeType::JOIN;
    }
    size_t armCount() const { return ctrlOutputs().size(); }
    Node *armHead(size_t index) const {
        auto co = ctrlOutputs();
        ASSERT(index < co.size(), "BRCH arm index out of range.");
        return co[index];
    }
    JoinNode *matchedJoin() const;

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

class JoinNode : public Node {
  public:
    JoinNode(Graph &graph, Type *type, data_idx_t index)
        : Node(graph, NodeType::JOIN, type, index) {}
    ~JoinNode() = default;

    static Node *create(Graph &graph, Type *type);

    bool hasMatchedBranch() const {
        auto ni = normInputs();
        return !ni.empty() && ni.front()->type() == NodeType::BRCH;
    }
    BrchNode *matchedBranch() const;
    bool hasBranchIndexInput() const { return !normInputs().empty(); }
    Node *branchIndexInput() const {
        ASSERT(!normInputs().empty(), "JOIN node must have a branch-index input.");
        return normInputs().front();
    }
    size_t armCount() const { return withInputs().size(); }
    Node *armTail(size_t index) const {
        auto wi = withInputs();
        ASSERT(index < wi.size(), "JOIN arm index out of range.");
        return wi[index];
    }

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

    bool hasCallee() const { return !withInputs().empty(); }
    Node *calleeInput() const {
        ASSERT(!withInputs().empty(), "CALL node has no callee input.");
        return withInputs().front();
    }
    size_t withArgCount() const { return withInputs().empty() ? 0 : withInputs().size() - 1; }
    Node *withArg(size_t index) const {
        auto wi = withInputs();
        ASSERT(index + 1 < wi.size(), "CALL with-arg index out of range.");
        return wi[index + 1];
    }
    node_span_t normArgs() const { return normInputs(); }

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
    ::Function *rtFunc_;
    Graph *graph_;

  public:
    FuncNode(Graph &graph, data_idx_t index, Graph *bodyGraph, ::Function *rtFunc)
        : Node(graph, NodeType::FUNC, bodyGraph->funcType()->exitType(), index), rtFunc_(rtFunc),
          graph_(bodyGraph) {}
    ~FuncNode() = default;

    static Node *create(Graph &graph, const graph_ptr_t &bodyGraph);
    static Node *create(Graph &graph, Graph *bodyGraph);

    ::Function *rtFunc() const { return rtFunc_; }
    Graph *bodyGraph() const { return graph_; }
    FunctionType *funcType() const;
    bool isMacro() const { return graph_ && graph_->isMacro(); }
    bool hasMatchedJoin() const {
        auto wo = withOutputs();
        return wo.size() == 1 && wo.front()->type() == NodeType::JOIN;
    }
    JoinNode *matchedJoin() const;

    std::string toString() const override;

    Node *clone(Graph &graph) const override;

  private:
    void setBodyGraph(Graph *bodyGraph) {
        ASSERT(bodyGraph != nullptr, "FuncNode body graph cannot be null.");
        ASSERT(!graph().finalized(), "Cannot retarget FuncNode after graph is sealed.");
        graph_ = bodyGraph;
        setDataType(bodyGraph->funcType()->exitType());
    }
    friend class detail::NodeMutation;
};

class OperNode : public Node {
    ::OperatorIndex *operator_;
    mutable ::operator_t cachedOp_ = nullptr;

  public:
    OperNode(Graph &graph, data_idx_t index, ::OperatorIndex *op)
        : Node(graph, NodeType::OPER, op->funcType()->exitType(), index), operator_(op) {}
    ~OperNode() = default;

    static Node *create(Graph &graph, oper_idx_ptr_t op);

    ::OperatorIndex *oper() const { return operator_; }
    FunctionType *funcType() const;

    ::operator_t getCachedOp() const { return cachedOp_; }
    void setCachedOp(::operator_t op) const { cachedOp_ = op; }

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

// 辅助节点
class DrefNode : public Node {
  public:
    // FUNC overload set / OPERATOR group / pre-built inlined graph.
    using dref_target_t = std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>;

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

class GateNode : public Node {
  public:
    GateNode(Graph &graph) : Node(graph, NodeType::GATE, Type::Void(), 0) {}
    ~GateNode() = default;

    static Node *create(Graph &graph);

    std::string toString() const override;

    Node *clone(Graph &graph) const override;
};

} // namespace camel::compile::gir
