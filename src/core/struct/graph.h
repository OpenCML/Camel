/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Aug. 13, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "data.h"
#include "function.h"
#include "operator.h"

#include <any>
#include <list>
#include <variant>

namespace GraphIR {

enum class NodeType { GRAPH, DATA, STRUCT, FUNCTOR, OPERATOR };

enum class DataType {
    STATIC_CONSTANT,  // shared among all copies of the graph and never changed
    RUNTIME_CONSTANT, // produced during runtime and never changed once produced
    STATIC_VARIABLE,  // shared among all copies of the graph and may be changed during runtime
    RUNTIME_VARIABLE, // produced during runtime and may be changed during runtime
};

class Node;

using node_ptr_t = std::shared_ptr<Node>;
using node_wptr_t = std::weak_ptr<Node>;
using node_lst_t = std::list<node_ptr_t>;
using node_vec_t = std::vector<node_ptr_t>;

class Graph;

using graph_ptr_t = std::shared_ptr<Graph>;

class Node : public std::enable_shared_from_this<Node> {
  protected:
    NodeType nodeType_;
    DataType dataType_;
    size_t dataIndex_;
    graph_ptr_t graph_ = nullptr;
    node_vec_t inputs_;
    node_vec_t outputs_;

  public:
    Node(NodeType type) : nodeType_(type) {};
    virtual ~Node() = default;

    NodeType type() const { return nodeType_; }
    data_ptr_t data() const;

    DataType dataType() const { return dataType_; }

    node_vec_t &inputs() { return inputs_; }
    node_vec_t &outputs() { return outputs_; }

    size_t inDegree() const { return inputs_.size(); }
    size_t outDegree() const { return outputs_.size(); }

    virtual data_ptr_t eval() {};

    static void link(node_ptr_t &from, node_ptr_t &to) {
        from->outputs().push_back(to);
        to->inputs().push_back(from);
    }
};

class Graph : public Node {
    node_ptr_t output_ = nullptr;
    std::shared_ptr<node_vec_t> nodes_;
    std::shared_ptr<node_vec_t> inputs_;

    std::shared_ptr<data_vec_t> staticConstants_;
    std::shared_ptr<data_vec_t> staticVariables_;
    std::shared_ptr<std::vector<size_t>> rtConstantIndices_;
    std::shared_ptr<std::vector<std::variant<size_t, data_ptr_t>>> runtimeConstants_;
    std::shared_ptr<std::vector<size_t>> rtVariableIndices_;
    std::shared_ptr<std::vector<std::variant<size_t, data_ptr_t>>> runtimeVariables_;

  public:
    Graph();
    Graph(Graph &other);
    ~Graph() = default;

    data_ptr_t getConstant(size_t index) const;
    data_ptr_t getVariable(size_t index) const;
    size_t addConstant(const data_ptr_t &data);
    size_t addVariable(const data_ptr_t &data);
    // set a constant to a variable, return the index of the variable
    size_t setVariable(size_t index);

    node_vec_t &nodes() { return *nodes_; }
    node_vec_t &inputs() { return *inputs_; }
    node_ptr_t &output() { return output_; }
};

class DataNode : public Node {

  public:
    DataNode(graph_ptr_t graph, const data_ptr_t &data);
    ~DataNode() = default;

    void setVariable();
};

inline std::shared_ptr<DataNode> data_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DataNode>(ptr);
}

class StructNode : public Node {
    size_t dataIndex_;

  public:
    StructNode(const data_ptr_t &data) : Node(NodeType::STRUCT) {}
    ~StructNode() = default;
};

inline std::shared_ptr<StructNode> struct_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<StructNode>(ptr);
}

class FunctorNode : public Node {
    func_ptr_t func_;
    graph_ptr_t graph_;
    node_ptr_t output_;
    node_ptr_t linkParams_;
    node_ptr_t withParams_;
    node_ptr_t closure_;

  public:
    FunctorNode(const func_ptr_t &func) : Node(NodeType::FUNCTOR), func_(func) {
        nodeType_ = NodeType::FUNCTOR;
        inputs_.resize(2, nullptr);
    }

    void setSuperParams(const node_ptr_t &superParams) { inputs_[0] = superParams; }
    void setParams(const node_ptr_t &params) { inputs_[1] = params; }

    node_ptr_t &superParams() { return inputs_[0]; }
    node_ptr_t &params() { return inputs_[1]; }

    ~FunctorNode() = default;
};

inline std::shared_ptr<FunctorNode> func_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FunctorNode>(ptr);
}

class OperatorNode : public Node {
    Operator *operation_;

  public:
    OperatorNode(Operator *operation) : Node(NodeType::OPERATOR), operation_(operation) {}
    ~OperatorNode() = default;
};

inline std::shared_ptr<OperatorNode> op_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<OperatorNode>(ptr);
}
} // namespace GraphIR

namespace gir = GraphIR;