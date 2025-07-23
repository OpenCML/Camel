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
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <any>
#include <list>
#include <variant>

#include "arena.h"
#include "data.h"
#include "data/other/func.h"
#include "operator.h"

namespace GraphIntermediateRepresentation {

enum class NodeType {
    Select,
    Access,   // Element accessed during runtime
    Struct,   // Runtime constructed data structure
    Literal,  // Compile-time constant
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
using node_ptr_t = std::shared_ptr<Node>;
using node_wptr_t = std::weak_ptr<Node>;
using node_lst_t = std::list<node_ptr_t>;
using node_vec_t = std::vector<node_ptr_t>;

class Graph {
  public:
    Graph(graph_ptr_t graph = nullptr, const std::string &name = "") : name_(name) {
        outer_ = graph;
        arena_ = std::make_shared<DataArena>();
    }
    ~Graph() = default;

    static graph_ptr_t create(graph_ptr_t graph = nullptr, const std::string &name = "") {
        return std::make_shared<Graph>(graph);
    }

    const std::string &name() const { return name_; }
    arena_ptr_t arena() const { return arena_; }
    graph_ptr_t outer() const {
        if (outer_.expired()) {
            return nullptr;
        }
        return outer_.lock();
    }

    void setFuncType(const func_type_ptr_t &type);
    func_type_ptr_t funcType() const;

    void addNode(const node_ptr_t &node);
    node_ptr_t addPort(bool isVar = false);

    void addSubGraph(const graph_ptr_t &graph);
    std::vector<graph_ptr_t> &subGraphs() { return subGraphs_; }

    const node_ptr_t &output() const { return output_; }
    void setOutput(const node_ptr_t &node);

    const std::vector<DataIndex> &ports() const { return ports_; }
    const node_vec_t &nodes() { return nodes_; }

    void fulfill(const data_vec_t &dataList);

  private:
    std::string name_;
    graph_wptr_t outer_;
    std::vector<graph_ptr_t> subGraphs_;

    func_type_ptr_t funcType_;
    arena_ptr_t arena_;

    node_vec_t nodes_;
    node_ptr_t output_;
    std::vector<DataIndex> ports_;
};

class Node : public std::enable_shared_from_this<Node> {
  public:
    Node(graph_ptr_t graph, NodeType type, DataIndex index) : graph_(graph), nodeType_(type), dataIndex_(index) {
        ASSERT(graph, "Graph is not set for Node.");
    }
    virtual ~Node() = default;

    static node_ptr_t create(graph_ptr_t graph, NodeType type, DataIndex index) {
        return std::make_shared<Node>(graph, type, index);
    }

    NodeType type() const { return nodeType_; }
    DataType dataType() const { return dataIndex_.type; }

    bool isRoot() const { return !graph_.lock(); }
    graph_ptr_t graph() const {
        ASSERT(graph_.lock(), "Graph is not set for Node.");
        return graph_.lock();
    }
    data_ptr_t data() const;
    const DataIndex &index() const { return dataIndex_; }

    node_vec_t &normInputs() { return normInputs_; }
    node_vec_t &withInputs() { return withInputs_; }
    node_vec_t &ctrlInputs() { return ctrlInputs_; }

    node_vec_t &dataOutputs() { return dataOutputs_; }
    node_vec_t &ctrlOutputs() { return ctrlOutputs_; }

    size_t inDegree() const { return normInputs_.size() + withInputs_.size() + ctrlInputs_.size(); }
    size_t outDegree() const { return dataOutputs_.size() + ctrlOutputs_.size(); }

    bool isSource() const { return inDegree() == 0; }
    bool isReturn() const { return outDegree() == 0; }

    void ref() { refs_++; }
    void unref() {
        if (refs_ > 0) {
            refs_--;
        }
    }
    size_t refCnt() const { return refs_; }

    virtual data_ptr_t eval() { return data(); };

    static void link(LinkType type, node_ptr_t &from, node_ptr_t &to) {
        switch (type) {
        case LinkType::Norm:
            from->dataOutputs().push_back(to);
            to->normInputs().push_back(from);
            break;
        case LinkType::With:
            from->dataOutputs().push_back(to);
            to->withInputs().push_back(from);
            break;
        case LinkType::Ctrl:
            from->ctrlOutputs().push_back(to);
            to->ctrlInputs().push_back(from);
            break;
        }
    }

  protected:
    size_t refs_ = 0;
    bool macro_ = false;
    bool const_ = false;

    graph_wptr_t graph_;

    NodeType nodeType_;
    DataIndex dataIndex_;

    node_vec_t normInputs_;
    node_vec_t withInputs_;
    node_vec_t ctrlInputs_;

    node_vec_t dataOutputs_;
    node_vec_t ctrlOutputs_;
};

class SelectNode : public Node {
  public:
    SelectNode(graph_ptr_t graph, DataIndex &index) : Node(graph, NodeType::Select, index) {}
    ~SelectNode() = default;

    static node_ptr_t create(graph_ptr_t graph, DataIndex &index) { return std::make_shared<SelectNode>(graph, index); }

    data_ptr_t eval() override { return nullptr; }
};

class AccessNode : public Node {
  public:
    AccessNode(graph_ptr_t graph, DataIndex &data, const std::string &index)
        : Node(graph, NodeType::Access, data), index_(index) {}
    AccessNode(graph_ptr_t graph, DataIndex &data, const size_t &index)
        : Node(graph, NodeType::Access, data), index_(index) {}
    ~AccessNode() = default;

    static node_ptr_t create(graph_ptr_t graph, DataIndex &data, const std::string &index) {
        return std::make_shared<AccessNode>(graph, data, index);
    }

    bool isNum() const { return std::holds_alternative<size_t>(index_); }
    template <typename T> T index() const { return std::get<T>(index_); }

    data_ptr_t eval() override { return nullptr; }

  private:
    std::variant<std::string, size_t> index_;
};

class StructNode : public Node {
  public:
    StructNode(graph_ptr_t graph, DataIndex &index) : Node(graph, NodeType::Struct, index) {}
    ~StructNode() = default;

    static node_ptr_t create(graph_ptr_t graph, DataIndex &index) { return std::make_shared<StructNode>(graph, index); }

    data_ptr_t eval() override { return nullptr; }
};

class LiteralNode : public Node {
  public:
    LiteralNode(graph_ptr_t graph, DataIndex &index) : Node(graph, NodeType::Literal, index) {}
    ~LiteralNode() = default;

    static node_ptr_t create(graph_ptr_t graph, DataIndex &index) {
        return std::make_shared<LiteralNode>(graph, index);
    }

    data_ptr_t eval() override { return nullptr; }
};

class OperatorNode : public Node {
    Operator operator_;

  public:
    OperatorNode(graph_ptr_t graph, DataIndex &index, Operator op)
        : Node(graph, NodeType::Operator, index), operator_(op) {}
    ~OperatorNode() = default;

    static node_ptr_t create(graph_ptr_t graph, DataIndex &index, Operator op) {
        return std::make_shared<OperatorNode>(graph, index, op);
    }

    std::string operName() const { return operator_.name(); }

    data_ptr_t eval() override { return nullptr; }
};

class FunctionNode : public Node {
  public:
    FunctionNode(graph_ptr_t graph, DataIndex &index) : Node(graph, NodeType::Function, index) {}
    ~FunctionNode() = default;

    static node_ptr_t create(graph_ptr_t graph, DataIndex &index) {
        return std::make_shared<FunctionNode>(graph, index);
    }

    data_ptr_t eval() override { return nullptr; }
};

} // namespace GraphIntermediateRepresentation

namespace GIR = GraphIntermediateRepresentation;
