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
 * Created: Aug. 17, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "graph.h"
#include "utils/log.h"

using namespace gir;

/*
Node
*/

data_ptr_t Node::data() const {
    assert(graph_ != nullptr, "Graph is not set");
    // return dataType_ ? graph_->getVariable(dataIndex_) : graph_->getConstant(dataIndex_);
}

/*
Graph
*/

gir::Graph::Graph() : Node(NodeType::GRAPH) {
    nodes_ = std::make_shared<node_vec_t>();
    inputs_ = std::make_shared<node_vec_t>();
    staticConstants_ = std::make_shared<data_vec_t>();
    rtVariableIndices_ = std::make_shared<std::vector<size_t>>();
    runtimeVariables_ = std::make_shared<std::vector<std::variant<size_t, data_ptr_t>>>();
}

gir::Graph::Graph(Graph &other) : Node(NodeType::GRAPH) {
    nodes_ = other.nodes_;
    inputs_ = other.inputs_;
    output_ = other.output_;
    staticConstants_ = other.staticConstants_;
    rtVariableIndices_ = other.rtVariableIndices_;
    runtimeVariables_ = std::make_shared<std::vector<std::variant<size_t, data_ptr_t>>>();
    for (const auto &idx : *rtVariableIndices_) {
        runtimeVariables_->push_back(idx);
    }
}

data_ptr_t gir::Graph::getConstant(size_t index) const {
    assert(index < staticConstants_->size(), "Constant index out of range");
    return staticConstants_->at(index);
}

data_ptr_t gir::Graph::getVariable(size_t index) const {
    assert(index < runtimeVariables_->size(), "Variable index out of range");
    auto var = runtimeVariables_->at(index);
    if (std::holds_alternative<size_t>(var)) {
        // on first access, replace the index with the copied(deep) actual data
        data_ptr_t data = staticConstants_->at(std::get<size_t>(var))->clone(true);
        runtimeVariables_->at(index) = data;
        return data;
    } else {
        return std::get<data_ptr_t>(var);
    }
}

size_t gir::Graph::addConstant(const data_ptr_t &data) {
    staticConstants_->push_back(data);
    return staticConstants_->size() - 1;
}

size_t gir::Graph::addVariable(const data_ptr_t &data) {
    staticConstants_->push_back(data);
    return setVariable(staticConstants_->size() - 1);
}

size_t gir::Graph::setVariable(size_t index) {
    assert(index < staticConstants_->size(), "Variable index out of range");
    runtimeVariables_->push_back(index);
    rtVariableIndices_->push_back(rtVariableIndices_->size());
    return runtimeVariables_->size() - 1;
}

/*
DataNode
*/

DataNode::DataNode(graph_ptr_t graph, const data_ptr_t &data) : Node(NodeType::DATA) {
    graph_ = graph;
}

void DataNode::setVariable() {
    assert(graph_ != nullptr, "Graph is not set for DataNode");
    // dataType_ = true;
    dataIndex_ = graph_->setVariable(dataIndex_);
}