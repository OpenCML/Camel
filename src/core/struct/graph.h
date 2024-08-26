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

#include "function.h"
#include "operator.h"
#include "data.h"

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
    node_vec_t nodes;

  public:
    Graph() { type_ = NodeType::GRAPH; }
    ~Graph() = default;

    void addNode(const node_ptr_t &node) { nodes.push_back(node); }
    void delNode(const node_ptr_t &node) { nodes.pop_back(); }
};

using graph_ptr_t = std::shared_ptr<Graph>;

class DataGraphNode : public GraphNode {
    data_ptr_t value;

  public:
    DataGraphNode(const data_ptr_t &value) : value(value) { type_ = NodeType::DATA; }
    ~DataGraphNode() = default;
};

class FuncGraphNode : public GraphNode {
    func_ptr_t func;

  public:
    FuncGraphNode(const func_ptr_t &func)
        : func(func) {
        type_ = NodeType::FUNCTOR;
        inputs_.resize(2, nullptr);
    }

    void setSuperParams(const node_ptr_t &superParams) { inputs_[0] = superParams; }
    void setParams(const node_ptr_t &params) { inputs_[1] = params; }

    node_ptr_t &superParams() { return inputs_[0]; }
    node_ptr_t &params() { return inputs_[1]; }

    ~FuncGraphNode() = default;
};

class OpGraphNode : public GraphNode {
    Operator *operation;

  public:
    OpGraphNode(Operator *operation) : operation(operation) { type_ = NodeType::OPERATOR; }
    ~OpGraphNode() = default;
};