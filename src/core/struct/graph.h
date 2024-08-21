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

#include "operator.h"
#include "function.h"
#include "value.h"

#include <any>
#include <list>

enum class NodeType { OPERATOR, DATA, FUNCTOR, GRAPH };

class Operator;
class GraphNode;

using node_ptr_t = std::shared_ptr<GraphNode>;
using node_wptr_t = std::weak_ptr<GraphNode>;
using node_lst_t = std::list<node_ptr_t>;

class GraphNode {
  protected:
    NodeType type_;
    node_lst_t inputs_;
    node_lst_t outputs_;

  public:
    GraphNode() = default;
    virtual ~GraphNode() = default;

    NodeType type() const { return type_; }

    node_lst_t &inputs() { return inputs_; }
    node_lst_t &outputs() { return outputs_; }

    size_t inDegree() const { return inputs_.size(); }
    size_t outDegree() const { return outputs_.size(); }

    static void link(node_ptr_t &from, node_ptr_t &to) {
        from->outputs().push_back(to);
        to->inputs().push_back(from);
    }
};

class Graph : public GraphNode {
    node_lst_t nodes;

  public:
    Graph() { type_ = NodeType::GRAPH; }
    ~Graph() = default;

    void addNode(const node_ptr_t &node) { nodes.push_back(node); }
    void delNode(const node_ptr_t &node) { nodes.remove(node); }
};

using graph_ptr_t = std::shared_ptr<Graph>;

class DataNode : public GraphNode {
    value_ptr_t data;

  public:
    DataNode(const value_ptr_t &data) : data(data) { type_ = NodeType::DATA; }
    ~DataNode() = default;
};

class FunctionNode:public GraphNode{
    func_ptr_t func;
    node_ptr_t params;
    node_ptr_t super;

  public:
    FunctionNode(const func_ptr_t &func, const node_ptr_t &params, const node_ptr_t &super)
        : func(func), params(params), super(super) {
        type_ = NodeType::FUNCTOR;
    }
    ~FunctionNode() = default;
};

class OperatorNode : public GraphNode {
    Operator *operation;

  public:
    OperatorNode(Operator *operation) : operation(operation) { type_ = NodeType::OPERATOR; }
    ~OperatorNode() = default;
};