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
    assert(graph_, "Graph is not set.");
    if (dataType_.variable) {
        return graph_->getVariable(dataIndex_);
    } else {
        return graph_->getConstant(dataIndex_);
    }
}

void Node::makeVariable(bool shared) {
    assert(graph_ != nullptr, "Graph is not set for Node.");
    assert(!dataType_.variable, "Node is already a variable.");
    dataIndex_ = graph_->makeVariable(dataIndex_, shared);
    dataType_.shared = shared;
}

/*
Graph
*/

gir::Graph::Graph()
    : Node(NodeType::GRAPH, DataType{}), nodes_(std::make_shared<node_vec_t>()),
      inputs_(std::make_shared<node_vec_t>()), sharedConstants_(std::make_shared<data_vec_t>()),
      sharedVariables_(std::make_shared<data_vec_t>()), rtVariableIndices_(std::make_shared<std::vector<InitIndex>>()),
      runtimeConstants_(), runtimeVariables_() {}

gir::Graph::Graph(Graph &other)
    : Node(NodeType::GRAPH, DataType{}), nodes_(other.nodes_), inputs_(other.inputs_), output_(other.output_),
      sharedConstants_(other.sharedConstants_), sharedVariables_(other.sharedVariables_),
      rtVariableIndices_(other.rtVariableIndices_), runtimeConstants_(other.runtimeConstants_.size()),
      runtimeVariables_() {
    for (const auto &idx : *rtVariableIndices_) {
        runtimeVariables_.push_back(idx);
    }
}

size_t gir::Graph::makeVariable(size_t index, bool shared) {
    if (shared) {
        assert(!dataType_.shared, "Cannot make a shared variable from a runtime constant.");
        sharedVariables_->push_back(sharedConstants_->at(index));
        return sharedVariables_->size() - 1;
    } else {
        InitIndex idx{index, false};
        rtVariableIndices_->push_back(idx);
        runtimeVariables_.push_back(idx);
        return runtimeVariables_.size() - 1;
    }
}

size_t gir::Graph::addConstant(const data_ptr_t &data, bool shared) {
    if (shared) {
        sharedConstants_->push_back(data);
        return sharedConstants_->size() - 1;
    } else {
        runtimeConstants_.push_back(data);
        return runtimeConstants_.size() - 1;
    }
}

data_ptr_t gir::Graph::getConstant(size_t index, bool shared) {
    if (shared) {
        assert(index < sharedConstants_->size(), "Constant index out of range");
        return sharedConstants_->at(index);
    } else {
        assert(index < runtimeConstants_.size(), "Constant index out of range");
        return runtimeConstants_.at(index);
    }
}

data_ptr_t gir::Graph::getVariable(size_t index, bool shared) {
    if (shared) {
        assert(index < sharedVariables_->size(), "Variable index out of range");
        return sharedVariables_->at(index);
    } else {
        assert(index < runtimeVariables_.size(), "Variable index out of range");
        auto &var = runtimeVariables_.at(index);
        if (std::holds_alternative<InitIndex>(var)) {
            // on first access, replace the index with the copied(deep) actual data
            InitIndex &i = std::get<InitIndex>(var);
            data_ptr_t data = nullptr;
            if (i.shared) {
                data = sharedConstants_->at(i.index)->clone(true);
            } else {
                data = runtimeConstants_.at(i.index)->clone(true);
            }
            var = data;
            return data;
        } else {
            return std::get<data_ptr_t>(var);
        }
    }
}

void gir::Graph::setConstant(size_t index, const data_ptr_t &data, bool shared) {
    if (shared) {
        assert(index < sharedConstants_->size(), "Constant index out of range");
        sharedConstants_->at(index) = data;
    } else {
        assert(index < runtimeConstants_.size(), "Constant index out of range");
        runtimeConstants_.at(index) = data;
    }
}

void gir::Graph::setVariable(size_t index, const data_ptr_t &data, bool shared) {
    if (shared) {
        assert(index < sharedVariables_->size(), "Variable index out of range");
        sharedVariables_->at(index) = data;
    } else {
        assert(index < runtimeVariables_.size(), "Variable index out of range");
        runtimeVariables_.at(index) = data;
    }
}

/*
DataNode
*/

DataNode::DataNode(graph_ptr_t graph, const data_ptr_t &data, bool shared)
    : Node(NodeType::DATA, DataType{shared, false}, graph) {
    assert(graph, "Graph is not set for DataNode.");
    dataIndex_ = graph->addConstant(data, shared);
}

/*
StructNode
*/

StructNode::StructNode(graph_ptr_t graph, const data_ptr_t &data, data_vec_t &unrefData)
    : Node(NodeType::STRUCT, DataType(DataTypeEnum::RUNTIME_CONSTANT), graph), unrefDataVec_(std::move(unrefData)) {
    assert(graph, "Graph is not set for StructNode.");
    // add data to graph as a runtime constant
    dataIndex_ = graph->addConstant(data, false);
}

data_ptr_t StructNode::eval() {
    data_ptr_t data = Node::data();
    // unref the data
    for (const auto &d : unrefDataVec_) {
        d->unref();
    }
    return data;
}