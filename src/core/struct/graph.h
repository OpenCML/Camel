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

#include "value.h"
#include "operation.h"

#include <any>
#include <list>

enum class NodeType { OPERATION, DATA, GRAPH };

class Operation;
class GraphNode;

using node_ptr_t = std::shared_ptr<GraphNode>;
using node_wptr_t = std::weak_ptr<GraphNode>;
using node_lst_t = std::list<node_ptr_t>;

class GraphNode {
  protected:
    NodeType type;
    node_lst_t inputs;
    node_lst_t outputs;
    bool computed = false;

  public:
    GraphNode() = default;
    virtual ~GraphNode() = default;

    bool isComputed() const { return computed; }
    void setComputed(bool value) { computed = value; }

    virtual void run() = 0;
};

class Graph : public GraphNode {
    node_lst_t nodes;

  public:
    Graph() { type = NodeType::GRAPH; }
    ~Graph() = default;

    void run() override;
};

class DataNode : public GraphNode {
    value_ptr_t data;

  public:
    DataNode(const value_ptr_t &data) : data(data) {
        type = NodeType::DATA;
        computed = true;
    }
    ~DataNode() = default;

    void run() override;
};

class OperationNode : public GraphNode {
    Operation *operation;

  public:
    OperationNode(Operation *operation) : operation(operation) { type = NodeType::OPERATION; }
    ~OperationNode() = default;

    void run() override;
};