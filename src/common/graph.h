/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <any>
#include <list>
#include <variant>

#include "data.h"
#include "operator.h"

#include "data/other/functor.h"

namespace GraphIR {

enum class NodeType { GRAPH, DATA, STRUCT, SELECT, FUNCTOR, OPERATOR };

std::ostream &operator<<(std::ostream &os, NodeType type);

enum class DataTypeEnum {
    SHARED_CONSTANT,  // shared among all copies of the graph and never changed
    SHARED_VARIABLE,  // produced during runtime and never changed once produced
    RUNTIME_CONSTANT, // shared among all copies of the graph and may be changed during runtime
    RUNTIME_VARIABLE  // produced during runtime and may be changed during runtime
};

std::ostream &operator<<(std::ostream &os, DataTypeEnum type);

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
using graph_wptr_t = std::weak_ptr<Graph>;

class Node : public std::enable_shared_from_this<Node> {
  protected:
    size_t refs_ = 0;
    NodeType nodeType_;
    DataType dataType_;
    graph_wptr_t graph_;
    size_t dataIndex_;

    node_vec_t inputs_;
    node_vec_t outputs_;

  public:
    Node(NodeType nodeType, DataType dataType, graph_ptr_t graph = nullptr)
        : nodeType_(nodeType), dataType_(dataType), graph_(graph) {};
    virtual ~Node() = default;

    NodeType type() const { return nodeType_; }
    DataType dataType() const { return dataType_; }

    void makeVariable(bool shared = false);

    graph_ptr_t outer() const { return graph_.lock(); }
    data_ptr_t data() const;
    size_t index() const { return dataIndex_; }

    node_vec_t &inputs() { return inputs_; }
    node_vec_t &outputs() { return outputs_; }

    size_t inDegree() const { return inputs_.size(); }
    size_t outDegree() const { return outputs_.size(); }

    void ref() { refs_++; }
    void unref() {
        if (refs_ > 0) {
            refs_--;
        }
    }
    size_t refCnt() const { return refs_; }

    virtual data_ptr_t eval() { return data(); };

    static void link(node_ptr_t &from, node_ptr_t &to, int index = -1) {
        if (index >= 0) {
            from->outputs().push_back(to);
            if (index > to->inputs().size()) {
                // this can only happen when the link target is a variable
                // etc. port, variable
                to->inputs().resize(index + 1);
            }
            to->inputs().at(index) = from;
        } else {
            from->outputs().push_back(to);
            to->inputs().push_back(from);
        }
    }
};

class Graph : public Node {
    struct InitIndex {
        size_t index;
        bool shared;
    };

    func_wptr_t func_;

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

    void setFunc(const func_ptr_t &func);
    func_ptr_t func() const;

    void addNode(const node_ptr_t &node);
    node_ptr_t addPort(bool isVar = false);
    void addSubGraph(const graph_ptr_t &graph);

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
    node_ptr_t &withNode() { return inputs_[0]; }
    node_ptr_t &linkNode() { return inputs_[1]; }

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

    std::string opName() const { return operator_.name(); }

    static node_ptr_t create(graph_ptr_t graph, Operator op);
};

inline std::shared_ptr<OperatorNode> op_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<OperatorNode>(ptr);
}

using op_node_ptr_t = std::shared_ptr<OperatorNode>;

class SelectNode : public Node {
    std::shared_ptr<node_vec_t> funcs_;
    std::shared_ptr<std::vector<operator_ptr_t>> ops_;

  public:
    SelectNode(graph_ptr_t graph, node_vec_t &cases);
    SelectNode(graph_ptr_t graph, std::vector<operator_ptr_t> &cases);
    ~SelectNode() = default;

    static node_ptr_t create(graph_ptr_t graph, node_vec_t &cases);
    static node_ptr_t create(graph_ptr_t graph, std::vector<operator_ptr_t> &cases);

    std::vector<func_type_ptr_t> types() const;
    node_ptr_t caseAt(size_t index);
};

inline std::shared_ptr<SelectNode> select_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<SelectNode>(ptr);
}

using select_node_ptr_t = std::shared_ptr<SelectNode>;

} // namespace GraphIR

namespace gir = GraphIR;
