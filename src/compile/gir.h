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
 * Updated: Oct. 01, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <any>
#include <list>
#include <unordered_set>
#include <variant>

#include "core/context/arena.h"
#include "core/data/data.h"
#include "core/data/other/func.h"
#include "core/operator.h"
#include "utils/log.h"
#include "utils/type.h"

namespace GraphIR {

enum class NodeType {
    DATA, // Compile-time constant
    PORT, // Port data (runtime)
    COPY, // A runtime copy of data
    FILL, // Structured data constructed during runtime
    ACCS, // Element accessed during runtime
    BRCH, // Conditional branch
    JOIN, // Conditional branch join
    CALL, // Invoke a function
    BIND, // Attach with params to a function
    FUNC, // (Sub)-Graph, function
    OPER, // Atomic operation
    EXIT, // Final output node
};

enum class LinkType {
    Norm, // node edge
    With, // with edge
    Ctrl, // control edge
};

std::string to_string(NodeType type);

class Graph;
class Node;

using graph_ptr_t = std::shared_ptr<Graph>;
using graph_wptr_t = std::weak_ptr<Graph>;
using graph_vec_t = std::vector<graph_ptr_t>;
using graph_vec_ptr_t = std::shared_ptr<graph_vec_t>;
using node_ptr_t = std::shared_ptr<Node>;
using node_wptr_t = std::weak_ptr<Node>;
using node_lst_t = std::list<node_ptr_t>;
using node_vec_t = std::vector<node_ptr_t>;
using node_set_t = std::unordered_set<node_ptr_t>;

struct WeakPtrHash {
    template <typename T> std::size_t operator()(const std::weak_ptr<T> &wp) const {
        if (auto sp = wp.lock()) {
            return std::hash<T *>()(sp.get());
        }
        return 0;
    }
};

struct WeakPtrEqual {
    template <typename T>
    bool operator()(const std::weak_ptr<T> &lhs, const std::weak_ptr<T> &rhs) const {
        return !lhs.owner_before(rhs) && !rhs.owner_before(lhs);
    }
};

class Graph : public std::enable_shared_from_this<Graph> {
  public:
    Graph(const Graph &other) = delete;            // 删除拷贝构造函数
    Graph &operator=(const Graph &other) = delete; // 删除赋值运算
    Graph(Graph &&other) = delete;                 // 删除移动构造函数
    Graph &operator=(Graph &&other) = delete;      // 删除移动赋值运算

    explicit Graph(const graph_ptr_t &graph = nullptr, const std::string &name = "") : name_(name) {
        outer_ = graph;
        EXEC_WHEN_DEBUG(
            l.in("GIR").debug("Created Graph: {}", name_.empty() ? "<anonymous>" : name_));
    }
    ~Graph() {
        EXEC_WHEN_DEBUG(
            l.in("GIR").debug("Destroyed Graph: {}", name_.empty() ? "<anonymous>" : name_));
    };

    static graph_ptr_t create(const graph_ptr_t &graph = nullptr, const std::string &name = "") {
        static int anonymousIdx = 0;
        std::string graphName = name.empty() ? std::format("__{}__", anonymousIdx++) : name;
        const auto newGraph = std::make_shared<Graph>(graph, graphName);
        if (graph) {
            graph->addSubGraph(newGraph);
        }
        return newGraph;
    }

    bool isRoot() const { return !outer_.lock(); }
    const std::string &name() const { return name_; }
    bool looped() const { return looped_; }
    bool empty() const { return nodes_.empty(); }
    graph_ptr_t outer() const {
        if (outer_.expired()) {
            return nullptr;
        }
        return outer_.lock();
    }
    size_t inDegree() const { return dependencies_.size(); }
    size_t outDegree() const { return dependents_.size(); }

    std::string toString() const {
        return std::format(
            "Graph({}, nodes: {}, subgraphs: {}, deps: {}, outs: {})",
            name_.empty() ? "<anonymous>" : name_,
            nodes_.size(),
            subGraphs_.size(),
            dependencies_.size(),
            dependents_.size());
    }

    void setFuncType(const func_type_ptr_t &type);
    bool hasFuncType() const { return funcType_ != nullptr; }
    func_type_ptr_t funcType() const;

    const data_vec_t &staticDataArr() const { return staticDataArr_; }
    size_t addStaticData(const data_ptr_t &data) {
        staticDataArr_.push_back(data);
        return staticDataArr_.size() - 1;
    }
    size_t addRuntimeData() { return runtimeDataSize_++; }
    void setStaticData(size_t index, const data_ptr_t &data) {
        ASSERT(index < staticDataArr_.size(), "Static data index out of range.");
        staticDataArr_[index] = data;
    }
    data_ptr_t getStaticData(size_t index) const {
        ASSERT(index < staticDataArr_.size(), "Static data index out of range.");
        return staticDataArr_[index];
    }
    size_t staticDataSize() const { return staticDataArr_.size(); }
    size_t runtimeDataSize() const { return runtimeDataSize_; }

    std::optional<graph_ptr_t> getSubGraph(const std::string &name) {
        if (subGraphs_.find(name) != subGraphs_.end()) {
            return subGraphs_[name];
        }
        return std::nullopt;
    }
    void addSubGraph(const graph_ptr_t &graph) {
        ASSERT(graph.get() != this, "Cannot add itself as a subgraph.");
        ASSERT(!graph->name().empty(), "Cannot add an anonymous graph as a subgraph.");
        ASSERT(
            subGraphs_.find(graph->name()) == subGraphs_.end(),
            std::format("Graph '{}' already has a subgraph named '{}'.", name_, graph->name()));
        subGraphs_[graph->name()] = graph;
        graph->outer_ = shared_from_this();
    }
    std::unordered_map<std::string, graph_ptr_t> &subGraphs() { return subGraphs_; }

    std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> &dependents() {
        return dependents_;
    }
    std::unordered_set<graph_ptr_t> &dependencies() { return dependencies_; }
    void addDependency(const graph_ptr_t &graph) {
        if (graph.get() == this) {
            this->looped_ = true;
            // Here we do not add itself to dependencies_ to avoid self-references
            // but only mark it as a looped graph
            return;
        }
        dependencies_.insert(graph);
        graph->dependents_.insert(shared_from_this());
    }

    void addNode(const node_ptr_t &node); // 由Node::create调用
    void addPort(const node_ptr_t &node); // 由PortNode::create调用
    void addCapture(const node_ptr_t &node);

    const node_ptr_t &exitNode() const {
        ASSERT(output_ != nullptr, "Graph has no output node.");
        return output_;
    }
    bool hasOutput() const { return output_ != nullptr; }
    void setOutput(const node_ptr_t &node);

    const node_vec_t &ports() const { return ports_; }
    const node_vec_t &nodes() { return nodes_; }
    const node_set_t &capture() const { return capture_; }

    graph_ptr_t clone() const;

  private:
    bool looped_ = false;
    std::string name_;
    graph_wptr_t outer_;

    std::unordered_map<std::string, graph_ptr_t> subGraphs_;
    std::unordered_set<graph_ptr_t> dependencies_;
    std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> dependents_;

    func_type_ptr_t funcType_;
    data_vec_t staticDataArr_;
    size_t runtimeDataSize_ = 0;

    node_vec_t ports_;
    node_vec_t nodes_;
    node_ptr_t output_;
    node_set_t capture_;
    node_set_t exposure_;
};

class Node : public std::enable_shared_from_this<Node> {
  public:
    Node(Graph &graph, NodeType type, size_t index)
        : graph_(graph), nodeType_(type), dataIndex_(index) {}
    virtual ~Node() = default;

    NodeType type() const { return nodeType_; }
    virtual std::string toString() const {
        return std::format("Node({}, {})", to_string(nodeType_), std::to_string(dataIndex_));
    }
    virtual operator std::string() const { return toString(); }
    virtual node_ptr_t clone(Graph &graph) const = 0;

    Graph &graph() const { return graph_; }
    size_t index() const { return dataIndex_; }
    bool macro() const { return macro_; }
    bool constant() const { return const_; }
    void setMacro(bool m) { macro_ = m; }
    void setConstant(bool c) { const_ = c; }

    node_vec_t dataInputs() const {
        node_vec_t inputs;
        inputs.insert(inputs.end(), withInputs_.begin(), withInputs_.end());
        inputs.insert(inputs.end(), normInputs_.begin(), normInputs_.end());
        return inputs;
    }
    node_vec_t &withInputs() { return withInputs_; }
    node_vec_t &normInputs() { return normInputs_; }
    node_vec_t &ctrlInputs() { return ctrlInputs_; }

    node_vec_t &dataOutputs() { return dataOutputs_; }
    node_vec_t &ctrlOutputs() { return ctrlOutputs_; }

    bool hasDeepLinkedTo(const node_ptr_t &node, size_t maxJumps = 99) const;
    bool hasLinkedTo(const node_ptr_t &node) const;

    size_t inDegree() const { return withInputs_.size() + normInputs_.size() + ctrlInputs_.size(); }
    size_t outDegree() const { return dataOutputs_.size() + ctrlOutputs_.size(); }

    bool isSource() const { return inDegree() == 0; }
    bool isReturn() const { return outDegree() == 0; }

    static void link(LinkType type, const node_ptr_t &from, const node_ptr_t &to);

  protected:
    bool macro_ = false;
    bool const_ = false;

    Graph &graph_;

    NodeType nodeType_;
    size_t dataIndex_;

    node_vec_t withInputs_;
    node_vec_t normInputs_;
    node_vec_t ctrlInputs_;

    node_vec_t dataOutputs_;
    node_vec_t ctrlOutputs_;
};

class DataNode : public Node {
  public:
    DataNode(Graph &graph, size_t index) : Node(graph, NodeType::DATA, index) {}
    ~DataNode() = default;

    static node_ptr_t create(Graph &graph, const data_ptr_t &data) {
        size_t index = graph.addStaticData(data);
        auto node = std::make_shared<DataNode>(graph, index);
        graph.addNode(node);
        return node;
    }

    data_ptr_t data() const { return graph_.getStaticData(dataIndex_); }

    virtual std::string toString() const override {
        return std::format("DATA({}): {}", dataIndex_, data()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return DataNode::create(graph, data());
    }
};

class PortNode : public Node {
    bool isWithArg_ = false;

  public:
    PortNode(Graph &graph, size_t index, bool isWith = false)
        : Node(graph, NodeType::PORT, index), isWithArg_(isWith) {}
    ~PortNode() = default;

    static node_ptr_t create(Graph &graph, bool isWithArg = false) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<PortNode>(graph, index, isWithArg);
        graph.addPort(node);
        return node;
    }

    bool isWithArg() const { return isWithArg_; }

    virtual std::string toString() const override { return std::format("PORT({})", dataIndex_); }

    virtual node_ptr_t clone(Graph &graph) const override {
        return PortNode::create(graph, isWithArg_);
    }
};

class CopyNode : public Node {
  public:
    CopyNode(Graph &graph, size_t index) : Node(graph, NodeType::COPY, index) {}
    ~CopyNode() = default;

    static node_ptr_t create(Graph &graph) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<CopyNode>(graph, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override { return std::format("COPY({})", dataIndex_); }

    virtual node_ptr_t clone(Graph &graph) const override { return CopyNode::create(graph); }
};

class FillNode : public Node {
    type_ptr_t dataType_;

  public:
    FillNode(Graph &graph, size_t index, type_ptr_t type)
        : Node(graph, NodeType::FILL, index), dataType_(type) {}
    ~FillNode() = default;

    static node_ptr_t create(Graph &graph, type_ptr_t type) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<FillNode>(graph, index, type);
        graph.addNode(node);
        return node;
    }

    type_ptr_t dataType() const { return dataType_; }

    virtual std::string toString() const override {
        return std::format("FILL({}): {}", dataIndex_, dataType()->toString());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return FillNode::create(graph, dataType_);
    }
};

class AccsNode : public Node {
  public:
    AccsNode(Graph &graph, size_t index, const std::variant<std::string, size_t> &accsIdx)
        : Node(graph, NodeType::ACCS, index), accsIndex_(accsIdx) {}
    ~AccsNode() = default;

    static node_ptr_t create(Graph &graph, const std::variant<std::string, size_t> &accsIdx) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<AccsNode>(graph, index, accsIdx);
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
        return std::format("ACCS({}): ${}", dataIndex_, index2String());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return AccsNode::create(graph, accsIndex_);
    }

  private:
    std::variant<std::string, size_t> accsIndex_;
};

class BrchNode : public Node {
  public:
    BrchNode(Graph &graph, size_t index) : Node(graph, NodeType::BRCH, index) {}
    ~BrchNode() = default;

    static node_ptr_t create(Graph &graph) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<BrchNode>(graph, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override { return std::format("BRCH({})", dataIndex_); }

    virtual node_ptr_t clone(Graph &graph) const override { return BrchNode::create(graph); }
};

class JoinNode : public Node {
  public:
    JoinNode(Graph &graph, size_t index) : Node(graph, NodeType::JOIN, index) {}
    ~JoinNode() = default;

    static node_ptr_t create(Graph &graph) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<JoinNode>(graph, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override { return std::format("JOIN({})", dataIndex_); }

    virtual node_ptr_t clone(Graph &graph) const override { return JoinNode::create(graph); }
};

class CallNode : public Node {
  public:
    CallNode(Graph &graph, size_t index) : Node(graph, NodeType::CALL, index) {}
    ~CallNode() = default;

    static node_ptr_t create(Graph &graph) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<CallNode>(graph, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override { return std::format("CALL({})", dataIndex_); }

    virtual node_ptr_t clone(Graph &graph) const override { return CallNode::create(graph); }
};

class BindNode : public Node {
  public:
    BindNode(Graph &graph, size_t index) : Node(graph, NodeType::BIND, index) {}
    ~BindNode() = default;

    static node_ptr_t create(Graph &graph) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<BindNode>(graph, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override { return std::format("BIND({})", dataIndex_); }

    virtual node_ptr_t clone(Graph &graph) const override { return BindNode::create(graph); }
};

class FuncNode : public Node {
    func_ptr_t func_;

  public:
    FuncNode(Graph &graph, size_t index, func_ptr_t func)
        : Node(graph, NodeType::FUNC, index), func_(func) {}
    ~FuncNode() = default;

    static node_ptr_t create(Graph &graph, func_ptr_t func) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<FuncNode>(graph, index, func);
        graph.addNode(node);
        return node;
    }

    func_ptr_t func() const { return func_; }
    func_type_ptr_t funcType() const {
        ASSERT(func_, "Function is not set for FunctionNode.");
        return tt::as_shared<FunctionType>(func_->type());
    }

    virtual std::string toString() const override {
        return std::format(
            "FUNC({}): {}",
            dataIndex_,
            func_->name().empty() ? func_->graph().name() : func_->name());
    }

    virtual node_ptr_t clone(Graph &graph) const override { return FuncNode::create(graph, func_); }
};

class OperNode : public Node {
    oper_idx_ptr_t operator_;

  public:
    OperNode(Graph &graph, size_t index, oper_idx_ptr_t op)
        : Node(graph, NodeType::OPER, index), operator_(op) {}
    ~OperNode() = default;

    static node_ptr_t create(Graph &graph, oper_idx_ptr_t op) {
        size_t index = graph.addRuntimeData();
        auto node = std::make_shared<OperNode>(graph, index, op);
        graph.addNode(node);
        return node;
    }

    oper_idx_ptr_t oper() const { return operator_; }
    func_type_ptr_t funcType() const {
        ASSERT(operator_, "Operator is not set for OperatorNode.");
        return tt::as_shared<FunctionType>(operator_->funcType());
    }

    virtual std::string toString() const override {
        return std::format("OPER({}): <{}>", dataIndex_, operator_->name());
    }

    virtual node_ptr_t clone(Graph &graph) const override {
        return OperNode::create(graph, operator_);
    }
};

class ExitNode : public Node {
  public:
    ExitNode(Graph &graph, size_t index) : Node(graph, NodeType::EXIT, index) {}
    ~ExitNode() = default;

    static node_ptr_t create(Graph &graph, size_t index = 0) {
        auto node = std::make_shared<ExitNode>(graph, index);
        graph.addNode(node);
        return node;
    }

    virtual std::string toString() const override { return std::format("EXIT({})", dataIndex_); }

    virtual node_ptr_t clone(Graph &graph) const override { return ExitNode::create(graph); }
};

} // namespace GraphIR
