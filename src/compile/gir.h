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
 * Updated: Sep. 26, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <any>
#include <list>
#include <variant>

#include "core/context/arena.h"
#include "core/data/data.h"
#include "core/data/other/func.h"
#include "core/operator.h"
#include "utils/log.h"
#include "utils/type.h"

namespace GraphIR {

enum class NodeType {
    Select,
    Access,   // Element accessed during runtime
    Struct,   // Runtime constructed data structure
    Source,   // Compile-time constant or port
    Return,   // Final output node
    Operator, // Atomic operation
    Function, // (Sub)-Graph, function
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
    Graph(graph_ptr_t graph = nullptr, const std::string &name = "") : name_(name), arena_() {
        outer_ = graph;
        arena_ = std::make_shared<DataArena>();
        EXEC_WHEN_DEBUG(
            l.in("GIR").debug("Created Graph: {}", name_.empty() ? "<anonymous>" : name_));
    }
    ~Graph() {
        EXEC_WHEN_DEBUG(
            l.in("GIR").debug("Destroyed Graph: {}", name_.empty() ? "<anonymous>" : name_));
    };

    static graph_ptr_t create(graph_ptr_t graph = nullptr, const std::string &name = "") {
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
    arena_ptr_t arena() { return arena_; }
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

    DataIndex addSharedConstant(const data_ptr_t &data) { return arena_->addConstant(data, true); }
    DataIndex addRuntimeConstant() { return arena_->addConstant(nullptr, false); }
    DataIndex addVariable(DataIndex index) { return arena_->addVariable(index); }

    void setFuncType(const func_type_ptr_t &type);
    bool hasFuncType() const { return funcType_ != nullptr; }
    func_type_ptr_t funcType() const;

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

    void addNode(const node_ptr_t &node);
    node_ptr_t addPort(bool isWithArg = false);

    const node_ptr_t &returnNode() const {
        ASSERT(output_ != nullptr, "Graph has no output node.");
        return output_;
    }
    bool hasOutput() const { return output_ != nullptr; }
    void setOutput(const node_ptr_t &node);

    const std::vector<std::pair<node_ptr_t, bool>> &portNodes() const { return ports_; }
    const node_vec_t &nodes() { return nodes_; }

  private:
    bool looped_ = false;
    std::string name_;
    graph_wptr_t outer_;

    std::unordered_map<std::string, graph_ptr_t> subGraphs_;
    std::unordered_set<graph_ptr_t> dependencies_;
    std::unordered_set<graph_wptr_t, WeakPtrHash, WeakPtrEqual> dependents_;

    func_type_ptr_t funcType_;
    arena_ptr_t arena_;

    node_vec_t nodes_;
    node_ptr_t output_;
    std::vector<std::pair<node_ptr_t, bool>> ports_;
};

class Node : public std::enable_shared_from_this<Node> {
  public:
    Node(graph_ptr_t graph, NodeType type, const DataIndex index)
        : graph_(graph), nodeType_(type), dataIndex_(index) {
        ASSERT(graph, "Graph is not set for Node.");
    }
    virtual ~Node() = default;

    NodeType type() const { return nodeType_; }
    DataType dataType() const { return dataIndex_.type; }
    virtual std::string data2str() const { return "null"; }
    virtual std::string toString() const {
        return std::format(
            "Node({}, {}, {})",
            to_string(nodeType_),
            std::string(dataIndex_.type),
            dataIndex_.index);
    }
    virtual operator std::string() const { return toString(); }

    graph_ptr_t graph() const {
        ASSERT(graph_.lock(), "Graph is not set for Node.");
        return graph_.lock();
    }
    DataIndex index() const { return dataIndex_; }

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

    graph_wptr_t graph_;

    NodeType nodeType_;
    DataIndex dataIndex_;

    node_vec_t withInputs_;
    node_vec_t normInputs_;
    node_vec_t ctrlInputs_;

    node_vec_t dataOutputs_;
    node_vec_t ctrlOutputs_;
};

class SelectNode : public Node {
  public:
    enum class SelectType {
        Branch, // Split into two branches
        Join,   // Join two branches
    };

    SelectNode(graph_ptr_t graph, const DataIndex &index, SelectType type = SelectType::Branch)
        : Node(graph, NodeType::Select, index), selectType_(type) {}
    ~SelectNode() = default;

    static node_ptr_t
    create(graph_ptr_t graph, const DataIndex &index, SelectType type = SelectType::Branch) {
        auto node = std::make_shared<SelectNode>(graph, index, type);
        graph->addNode(node);
        return node;
    }

    SelectType selectType() const { return selectType_; }

    std::string toString() const override {
        return std::format(
            "Select({}, {}): {}",
            std::string(dataIndex_.type),
            dataIndex_.index,
            selectType_ == SelectType::Branch ? "BRCH" : "JOIN");
    }

  private:
    SelectType selectType_;
};

class AccessNode : public Node {
  public:
    AccessNode(
        graph_ptr_t graph, const DataIndex &data, const std::variant<std::string, size_t> &index)
        : Node(graph, NodeType::Access, data), index_(index) {}
    ~AccessNode() = default;

    static node_ptr_t create(
        graph_ptr_t graph, const DataIndex &data, const std::variant<std::string, size_t> &index) {
        auto node = std::make_shared<AccessNode>(graph, data, index);
        graph->addNode(node);
        return node;
    }

    bool isNum() const { return std::holds_alternative<size_t>(index_); }
    template <typename T> T index() const { return std::get<T>(index_); }
    std::string index2String() const {
        if (std::holds_alternative<size_t>(index_)) {
            return std::to_string(std::get<size_t>(index_));
        } else {
            return std::get<std::string>(index_);
        }
    }

    std::string data2str() const override { return std::format("#{}", index2String()); }
    std::string toString() const override {
        return std::format(
            "Access({}, {}): ${}",
            std::string(dataIndex_.type),
            dataIndex_.index,
            index2String());
    }

  private:
    std::variant<std::string, size_t> index_;
};

class StructNode : public Node {
    type_ptr_t dataType_;

  public:
    StructNode(graph_ptr_t graph, const DataIndex &index, type_ptr_t type)
        : Node(graph, NodeType::Struct, index), dataType_(type) {}
    ~StructNode() = default;

    static node_ptr_t create(graph_ptr_t graph, const DataIndex &index, type_ptr_t type) {
        auto node = std::make_shared<StructNode>(graph, index, type);
        graph->addNode(node);
        return node;
    }

    type_ptr_t dataType() const { return dataType_; }

    std::string data2str() const override { return std::format("{}", dataType()->toString()); }
    std::string toString() const override {
        return std::format(
            "Struct({}, {}): {}",
            std::string(dataIndex_.type),
            dataIndex_.index,
            dataType()->toString());
    }
};

class SourceNode : public Node {
    bool isPort_;

  public:
    SourceNode(graph_ptr_t graph, const DataIndex &index, bool isPort = false)
        : Node(graph, NodeType::Source, index), isPort_(isPort) {}
    ~SourceNode() = default;

    static node_ptr_t create(graph_ptr_t graph, const DataIndex &index, bool isPort = false) {
        auto node = std::make_shared<SourceNode>(graph, index, isPort);
        graph->addNode(node);
        return node;
    }

    data_ptr_t dataOf(const DataArena &arena) const {
        ASSERT(arena.has(dataIndex_), "Source data not found in arena.");
        return arena.get(dataIndex_);
    }

    std::string data2str() const override {
        ASSERT(graph_.lock(), "Graph is not set for Node.");
        const auto &arena = graph_.lock()->arena();
        return arena->has(dataIndex_) ? dataOf(*arena)->toString() : "null";
    }
    std::string toString() const override {
        ASSERT(graph_.lock(), "Graph is not set for Node.");
        const auto &arena = graph_.lock()->arena();
        return std::format(
            "Source({}, {}): {}",
            std::string(dataIndex_.type),
            dataIndex_.index,
            isPort_ ? "<PORT>" : dataOf(*arena)->toString());
    }
};

class ReturnNode : public Node {
  public:
    ReturnNode(graph_ptr_t graph, const DataIndex &index) : Node(graph, NodeType::Return, index) {}
    ~ReturnNode() = default;

    static node_ptr_t create(graph_ptr_t graph, const DataIndex &index) {
        auto node = std::make_shared<ReturnNode>(graph, index);
        graph->addNode(node);
        return node;
    }

    std::string toString() const override { return std::string("Node(Return)"); }
};

class OperatorNode : public Node {
    oper_idx_ptr_t operator_;

  public:
    OperatorNode(graph_ptr_t graph, const DataIndex &index, oper_idx_ptr_t op)
        : Node(graph, NodeType::Operator, index), operator_(op) {}
    ~OperatorNode() = default;

    static node_ptr_t create(graph_ptr_t graph, const DataIndex &index, oper_idx_ptr_t op) {
        auto node = std::make_shared<OperatorNode>(graph, index, op);
        graph->addNode(node);
        return node;
    }

    oper_idx_ptr_t oper() const { return operator_; }
    func_type_ptr_t funcType() const {
        ASSERT(operator_, "Operator is not set for OperatorNode.");
        return tt::as_shared<FunctionType>(operator_->funcType());
    }

    std::string data2str() const override { return std::format("<{}>", operator_->name()); }
    std::string toString() const override {
        return std::format(
            "Opera.({}, {}): {}",
            std::string(dataIndex_.type),
            dataIndex_.index,
            operator_->name());
    }
};

class FunctionNode : public Node {
    func_ptr_t func_;

  public:
    FunctionNode(graph_ptr_t graph, const DataIndex &index, func_ptr_t func)
        : Node(graph, NodeType::Function, index), func_(func) {}
    ~FunctionNode() = default;

    static node_ptr_t create(graph_ptr_t graph, const DataIndex &index, func_ptr_t func) {
        auto node = std::make_shared<FunctionNode>(graph, index, func);
        graph->addNode(node);
        return node;
    }

    func_ptr_t func() const { return func_; }
    func_type_ptr_t funcType() const {
        ASSERT(func_, "Function is not set for FunctionNode.");
        return tt::as_shared<FunctionType>(func_->type());
    }

    std::string data2str() const override {
        return std::format("<{}>", func_->name().empty() ? func_->graph()->name() : func_->name());
    }
    std::string toString() const override {
        return std::format(
            "Funct.({}, {}): {}",
            std::string(dataIndex_.type),
            dataIndex_.index,
            func_->name().empty() ? func_->graph()->name() : func_->name());
    }
};

} // namespace GraphIR
