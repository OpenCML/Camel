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

enum class NodeType { OPERATOR, DATA, FUNCTOR, GRAPH };

class GraphNode;

using node_ptr_t = std::shared_ptr<GraphNode>;
using node_wptr_t = std::weak_ptr<GraphNode>;
using node_lst_t = std::list<node_ptr_t>;
using node_vec_t = std::vector<node_ptr_t>;

class GraphNode {
  protected:
    NodeType type_;
    node_vec_t inputs_;
    node_vec_t outputs_;

  public:
    GraphNode() = default;
    virtual ~GraphNode() = default;

    NodeType type() const { return type_; }

    node_vec_t &inputs() { return inputs_; }
    node_vec_t &outputs() { return outputs_; }

    size_t inDegree() const { return inputs_.size(); }
    size_t outDegree() const { return outputs_.size(); }

    static void link(node_ptr_t &from, node_ptr_t &to) {
        from->outputs().push_back(to);
        to->inputs().push_back(from);
    }
};

class Graph : public GraphNode {
    node_vec_t nodes_;
    node_vec_t params_;
    node_vec_t superParams_;
    node_ptr_t output_;

  public:
    Graph() { type_ = NodeType::GRAPH; }
    ~Graph() = default;

    node_vec_t &nodes() { return nodes_; }
    node_vec_t &params() { return params_; }
    node_vec_t &superParams() { return superParams_; }
    node_ptr_t &output() { return output_; }

    void setOutput(node_ptr_t &&output) { output_ = std::move(output); }
    void setParams(node_vec_t &&params) { params_ = std::move(params); }
    void setSuperParams(node_vec_t &&superParams) { superParams_ = std::move(superParams); }

    void addNode(const node_ptr_t &node) { nodes_.push_back(node); }
    void delNode(const node_ptr_t &node) { nodes_.pop_back(); }
};

using graph_ptr_t = std::shared_ptr<Graph>;

class DataGraphNode : public GraphNode {
    data_ptr_t data_;

  public:
    DataGraphNode(const data_ptr_t &data) : data_(data) { type_ = NodeType::DATA; }
    ~DataGraphNode() = default;
};

inline std::shared_ptr<DataGraphNode> data_graph_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<DataGraphNode>(ptr);
}

class FuncGraphNode : public GraphNode {
    func_ptr_t func_;
    graph_ptr_t graph_;

  public:
    FuncGraphNode(const func_ptr_t &func) : func_(func) {
        type_ = NodeType::FUNCTOR;
        inputs_.resize(2, nullptr);
    }

    void setSuperParams(const node_ptr_t &superParams) { inputs_[0] = superParams; }
    void setParams(const node_ptr_t &params) { inputs_[1] = params; }

    node_ptr_t &superParams() { return inputs_[0]; }
    node_ptr_t &params() { return inputs_[1]; }

    ~FuncGraphNode() = default;
};

inline std::shared_ptr<FuncGraphNode> func_graph_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<FuncGraphNode>(ptr);
}

class OpGraphNode : public GraphNode {
    Operator *operation_;

  public:
    OpGraphNode(Operator *operation) : operation_(operation) { type_ = NodeType::OPERATOR; }
    ~OpGraphNode() = default;
};

inline std::shared_ptr<OpGraphNode> op_graph_node_ptr_cast(const node_ptr_t &ptr) {
    return std::dynamic_pointer_cast<OpGraphNode>(ptr);
}