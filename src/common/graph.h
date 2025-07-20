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

#include "data.h"
#include "operator.h"

#include "data/other/functor.h"

namespace GraphIntermediateRepresentation {

enum class DependType {
    Source, // 0 in-degree
    Normal, // non-zero in/out-degree, all inputs and outputs are connected
    Select, // non-zero in/out-degree, but not all inputs and outputs are connected
    Return, // 0 out-degree
};

enum class ActionType {
    Graph,    // (Sub)-Graph, function
    Access,   // Element accessed during runtime
    Struct,   // Runtime constructed data structure
    Literal,  // Compile-time constant
    Operator, // Atomic operation
};

enum class LinkType {
    Norm, // node edge
    With, // with edge
    Ctrl, // control edge
};

enum class OutputType {
    StaticConstant = 0b00,  // Compile-time constant, shared among all copies of the graph and never changed
    StaticVariable = 0b01,  // Compile-time variable, shared among graphs and may be changed during runtime
    RuntimeConstant = 0b10, // Produced during runtime and never changed, not shared
    RuntimeVariable = 0b11, // Produced during runtime and may be changed, not shared
};

std::string to_string(DependType type);
std::string to_string(ActionType type);
std::string to_string(OutputType type);

constexpr bool isStatic(OutputType type) { return (static_cast<int>(type) & 0b10) == 0; }
constexpr bool isConstant(OutputType type) { return (static_cast<int>(type) & 0b01) == 0; }

class Node;

using node_ptr_t = std::shared_ptr<Node>;
using node_wptr_t = std::weak_ptr<Node>;
using node_lst_t = std::list<node_ptr_t>;
using node_vec_t = std::vector<node_ptr_t>;

class Graph;

using graph_ptr_t = std::shared_ptr<Graph>;
using graph_wptr_t = std::weak_ptr<Graph>;

class Node : public std::enable_shared_from_this<Node> {

  public:
    Node(DependType dType, ActionType aType, OutputType oType, graph_ptr_t outer = nullptr)
        : dependType_(dType), actionType_(aType), outputType_(oType), outerGraph_(outer) {}
    virtual ~Node() = default;

    DependType dependType() const { return dependType_; }
    ActionType actionType() const { return actionType_; }
    OutputType outputType() const { return outputType_; }

    void makeVariable(bool shared = false);

    bool isRoot() const { return !outerGraph_.lock(); }
    graph_ptr_t outer() const {
        ASSERT(outerGraph_.lock(), "Graph is not set for Node.");
        return outerGraph_.lock();
    }
    data_ptr_t data() const;
    size_t index() const { return dataIndex_; }

    node_vec_t &normInputs() { return normInputs_; }
    node_vec_t &withInputs() { return withInputs_; }
    node_vec_t &ctrlInputs() { return ctrlInputs_; }

    node_vec_t &dataOutputs() { return dataOutputs_; }
    node_vec_t &ctrlOutputs() { return ctrlOutputs_; }

    size_t inDegree() const { return normInputs_.size() + withInputs_.size() + ctrlInputs_.size(); }
    size_t outDegree() const { return dataOutputs_.size() + ctrlOutputs_.size(); }

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

    DependType dependType_;
    ActionType actionType_;
    OutputType outputType_;

    graph_wptr_t outerGraph_;
    size_t dataIndex_;

    node_vec_t normInputs_;
    node_vec_t withInputs_;
    node_vec_t ctrlInputs_;

    node_vec_t dataOutputs_;
    node_vec_t ctrlOutputs_;
};

class Graph : public Node {
    struct InitIndex {
        size_t index;
        bool shared;
    };

    func_type_ptr_t funcType_;

    node_ptr_t output_;
    std::shared_ptr<node_vec_t> nodes_;
    // nodeIndex, dataIndex, isVar
    std::shared_ptr<std::vector<std::tuple<size_t, size_t, bool>>> ports_;
    std::vector<graph_ptr_t> subGraphs_;

    std::shared_ptr<data_vec_t> sharedConstants_;
    std::shared_ptr<data_vec_t> sharedVariables_;
    std::vector<data_ptr_t> runtimeConstants_;
    std::shared_ptr<std::vector<InitIndex>> rtVariableIndices_;
    std::vector<std::variant<InitIndex, data_ptr_t>> runtimeVariables_;

  public:
    Graph();
    Graph(Graph &other);
    ~Graph() = default;

    static graph_ptr_t create(graph_ptr_t graph = nullptr);

    void setFuncType(const func_type_ptr_t &type);
    func_type_ptr_t funcType() const;

    void addNode(const node_ptr_t &node);
    node_ptr_t addPort(bool isVar = false);

    void addSubGraph(const graph_ptr_t &graph);
    std::vector<graph_ptr_t> &subGraphs() { return subGraphs_; }

    const node_ptr_t &output() const { return output_; }
    void setOutput(const node_ptr_t &node);

    // set a constant to a variable, return the index of the variable
    size_t makeVariable(size_t index, bool shared = false);
    size_t addConstant(const data_ptr_t &data, bool shared = false);
    // there is no addVariable() function
    // because variables are created by makeVariable() function
    data_ptr_t getConstant(size_t index, bool shared = false);
    data_ptr_t getVariable(size_t index, bool shared = false);
    void setConstant(size_t index, const data_ptr_t &data, bool shared = false);
    void setVariable(size_t index, const data_ptr_t &data, bool shared = false);

    const std::vector<std::tuple<size_t, size_t, bool>> &ports() const { return *ports_; }
    node_vec_t &nodes() { return *nodes_; }

    void fulfill(const data_vec_t &dataList);

    virtual data_ptr_t eval() override;
};

class DataNode : public Node {
  public:
    DataNode(graph_ptr_t graph, const data_ptr_t &data, bool shared = false);
    ~DataNode() = default;

    static node_ptr_t create(graph_ptr_t graph, const data_ptr_t &data, bool shared = false);
};

inline std::shared_ptr<DataNode> data_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DataNode>(ptr);
}

using data_node_ptr_t = std::shared_ptr<DataNode>;

class StructNode : public Node {
  public:
    StructNode(graph_ptr_t graph, const data_ptr_t &data);
    ~StructNode() = default;

    static node_ptr_t create(graph_ptr_t graph, const data_ptr_t &data);
    virtual data_ptr_t eval() override;
};

inline std::shared_ptr<StructNode> struct_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<StructNode>(ptr);
}

using struct_node_ptr_t = std::shared_ptr<StructNode>;

class FunctorNode : public Node {
    func_ptr_t func_;

  public:
    FunctorNode(graph_ptr_t graph, const func_ptr_t &func);
    ~FunctorNode() = default;

    static node_ptr_t create(graph_ptr_t graph, const func_ptr_t &func);

    func_ptr_t func() const;
    func_type_ptr_t type() const;
    std::shared_ptr<FunctorNode> copyTo(graph_ptr_t graph) const;

    graph_ptr_t subGraph() const { return func_->graph(); }
    node_ptr_t &withNode() { return normInputs_[0]; }
    node_ptr_t &linkNode() { return normInputs_[1]; }

    void fulfill();

    virtual data_ptr_t eval() override;
};

inline std::shared_ptr<FunctorNode> func_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FunctorNode>(ptr);
}

using func_node_ptr_t = std::shared_ptr<FunctorNode>;

class OperatorNode : public Node {
    Operator operator_;

  public:
    OperatorNode(graph_ptr_t graph, Operator op);
    ~OperatorNode() = default;

    std::string operName() const { return operator_.name(); }

    static node_ptr_t create(graph_ptr_t graph, Operator op);
};

inline std::shared_ptr<OperatorNode> oper_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<OperatorNode>(ptr);
}

using oper_node_ptr_t = std::shared_ptr<OperatorNode>;

class SelectNode : public Node {
    std::shared_ptr<func_vec_t> funcs_;
    std::shared_ptr<oper_vec_t> opers_;

  public:
    SelectNode(graph_ptr_t graph, const func_vec_t &cases);
    SelectNode(graph_ptr_t graph, const oper_vec_t &cases);
    ~SelectNode() = default;

    static node_ptr_t create(graph_ptr_t graph, const func_vec_t &cases);
    static node_ptr_t create(graph_ptr_t graph, const oper_vec_t &cases);

    std::vector<func_type_ptr_t> types() const;
    node_ptr_t select(size_t index);
};

inline std::shared_ptr<SelectNode> select_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<SelectNode>(ptr);
}

using select_node_ptr_t = std::shared_ptr<SelectNode>;

} // namespace GraphIntermediateRepresentation

namespace GIR = GraphIntermediateRepresentation;
