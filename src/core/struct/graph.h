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

enum class DataTypeEnum {
    SHARED_CONSTANT,  // shared among all copies of the graph and never changed
    SHARED_VARIABLE,  // produced during runtime and never changed once produced
    RUNTIME_CONSTANT, // shared among all copies of the graph and may be changed during runtime
    RUNTIME_VARIABLE  // produced during runtime and may be changed during runtime
};

struct DataType {
    bool shared;
    bool variable;

    DataType(bool shared = false, bool variable = false) : shared(shared), variable(variable) {}
    constexpr DataType(DataTypeEnum type) : shared(false), variable(false) {
        switch (type) {
        case DataTypeEnum::SHARED_CONSTANT:
            shared = true;
            variable = false;
            break;
        case DataTypeEnum::SHARED_VARIABLE:
            shared = true;
            variable = true;
            break;
        case DataTypeEnum::RUNTIME_CONSTANT:
            shared = false;
            variable = false;
            break;
        case DataTypeEnum::RUNTIME_VARIABLE:
            shared = false;
            variable = true;
            break;
        }
    }
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
    graph_ptr_t graph_;
    size_t dataIndex_;

    node_vec_t inputs_;
    node_vec_t outputs_;

  public:
    Node(NodeType nodeType, DataType dataType, graph_ptr_t graph = nullptr)
        : nodeType_(nodeType), dataType_(dataType), graph_(graph) {};
    virtual ~Node() = default;

    NodeType nodeType() const { return nodeType_; }
    DataType dataType() const { return dataType_; }

    void makeVariable(bool shared = false);

    graph_ptr_t graph() const { return graph_; }
    data_ptr_t data() const;

    node_vec_t &inputs() { return inputs_; }
    node_vec_t &outputs() { return outputs_; }

    size_t inDegree() const { return inputs_.size(); }
    size_t outDegree() const { return outputs_.size(); }

    virtual data_ptr_t eval() { return data(); };

    static void link(node_ptr_t &from, node_ptr_t &to) {
        from->outputs().push_back(to);
        to->inputs().push_back(from);
    }
};

class Graph : public Node {
    struct InitIndex {
        size_t index;
        bool shared;
    };

    node_ptr_t output_;
    std::shared_ptr<node_vec_t> nodes_;

    std::shared_ptr<data_vec_t> sharedConstants_;
    std::shared_ptr<data_vec_t> sharedVariables_;
    std::vector<data_ptr_t> runtimeConstants_;
    std::shared_ptr<std::vector<InitIndex>> rtVariableIndices_;
    std::vector<std::variant<InitIndex, data_ptr_t>> runtimeVariables_;

  public:
    Graph();
    Graph(Graph &other);
    ~Graph() = default;

    void addNode(const node_ptr_t &node) { nodes_->push_back(node); }

    // set a constant to a variable, return the index of the variable
    size_t makeVariable(size_t index, bool shared = false);
    size_t addConstant(const data_ptr_t &data, bool shared = false);
    // there is no addVariable() function
    // because variables are created by makeVariable() function
    data_ptr_t getConstant(size_t index, bool shared = false);
    data_ptr_t getVariable(size_t index, bool shared = false);
    void setConstant(size_t index, const data_ptr_t &data, bool shared = false);
    void setVariable(size_t index, const data_ptr_t &data, bool shared = false);

    node_vec_t &nodes() { return *nodes_; }
};

class DataNode : public Node {
  private:
    DataNode(graph_ptr_t graph, const data_ptr_t &data, bool shared = false);
    ~DataNode() = default;

  public:
    static node_ptr_t create(graph_ptr_t graph, const data_ptr_t &data, bool shared = false);
};

inline std::shared_ptr<DataNode> data_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DataNode>(ptr);
}

class StructNode : public Node {
  private:
    StructNode(graph_ptr_t graph, const data_ptr_t &data);
    ~StructNode() = default;

  public:
    static node_ptr_t create(graph_ptr_t graph, const data_ptr_t &data);
    virtual data_ptr_t eval() override;
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