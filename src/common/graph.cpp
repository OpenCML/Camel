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
 * Created: Aug. 17, 2024
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include "graph.h"
#include "utils/log.h"

using namespace std;
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
    : Node(NodeType::GRAPH, DataType{}), nodes_(make_shared<node_vec_t>()),
      ports_(make_shared<vector<pair<size_t, bool>>>()), sharedConstants_(make_shared<data_vec_t>()),
      sharedVariables_(make_shared<data_vec_t>()), rtVariableIndices_(make_shared<vector<InitIndex>>()),
      runtimeConstants_(), runtimeVariables_() {}

gir::Graph::Graph(Graph &other)
    : Node(NodeType::GRAPH, DataType{}), nodes_(other.nodes_), ports_(other.ports_),
      sharedConstants_(other.sharedConstants_), sharedVariables_(other.sharedVariables_),
      rtVariableIndices_(other.rtVariableIndices_), runtimeConstants_(other.runtimeConstants_.size()),
      runtimeVariables_() {
    for (const auto &idx : *rtVariableIndices_) {
        runtimeVariables_.push_back(idx);
    }
}

void gir::Graph::addNode(const node_ptr_t &node) { nodes_->push_back(node); }

node_ptr_t gir::Graph::addPort(bool isVar = false) {
    node_ptr_t node = DataNode::create(dynamic_pointer_cast<Graph>(shared_from_this()), make_shared<NullData>(), false);
    if (isVar) {
        node->makeVariable();
    }
    size_t idx = node->index();
    ports_->push_back({idx, isVar});
    return node;
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
        assert(index < sharedConstants_->size(), "Constant index out of range.");
        return sharedConstants_->at(index);
    } else {
        assert(index < runtimeConstants_.size(), "Constant index out of range.");
        return runtimeConstants_.at(index);
    }
}

data_ptr_t gir::Graph::getVariable(size_t index, bool shared) {
    if (shared) {
        assert(index < sharedVariables_->size(), "Variable index out of range.");
        return sharedVariables_->at(index);
    } else {
        assert(index < runtimeVariables_.size(), "Variable index out of range.");
        auto &var = runtimeVariables_.at(index);
        if (holds_alternative<InitIndex>(var)) {
            // on first access, replace the index with the copied(deep) actual data
            InitIndex &i = get<InitIndex>(var);
            data_ptr_t data = nullptr;
            if (i.shared) {
                data = sharedConstants_->at(i.index)->clone(true);
            } else {
                data = runtimeConstants_.at(i.index)->clone(true);
            }
            var = data;
            return data;
        } else {
            return get<data_ptr_t>(var);
        }
    }
}

void gir::Graph::setConstant(size_t index, const data_ptr_t &data, bool shared) {
    if (shared) {
        assert(index < sharedConstants_->size(), "Constant index out of range.");
        sharedConstants_->at(index) = data;
    } else {
        assert(index < runtimeConstants_.size(), "Constant index out of range.");
        runtimeConstants_.at(index) = data;
    }
}

void gir::Graph::setVariable(size_t index, const data_ptr_t &data, bool shared) {
    if (shared) {
        assert(index < sharedVariables_->size(), "Variable index out of range.");
        sharedVariables_->at(index) = data;
    } else {
        assert(index < runtimeVariables_.size(), "Variable index out of range.");
        runtimeVariables_.at(index) = data;
    }
}

void gir::Graph::fulfill(const data_vec_t &dataList) {
    assert(dataList.size() == ports_->size(), "Data list size does not match ports size.");
    for (size_t i = 0; i < dataList.size(); i++) {
        const auto &data = dataList[i];
        const auto &[index, isVar] = ports_->at(i);
        if (isVar) {
            setVariable(index, data, false);
        } else {
            setConstant(index, data, false);
        }
    }
}

data_ptr_t gir::Graph::eval() {}

/*
DataNode
*/

DataNode::DataNode(graph_ptr_t graph, const data_ptr_t &data, bool shared)
    : Node(NodeType::DATA, DataType{shared, false}, graph) {
    assert(graph, "Graph is not set for DataNode.");
    dataIndex_ = graph->addConstant(data, shared);
}

node_ptr_t DataNode::create(graph_ptr_t graph, const data_ptr_t &data, bool shared) {
    const auto res = make_shared<DataNode>(graph, data, shared);
    graph->addNode(res);
    return res;
}

/*
StructNode
*/

StructNode::StructNode(graph_ptr_t graph, const data_ptr_t &data)
    : Node(NodeType::STRUCT, DataType(DataTypeEnum::RUNTIME_CONSTANT), graph) {
    assert(graph, "Graph is not set for StructNode.");
    // add data to graph as a runtime constant
    dataIndex_ = graph->addConstant(data, false);
}

node_ptr_t StructNode::create(graph_ptr_t graph, const data_ptr_t &data) {
    const auto res = make_shared<StructNode>(graph, data);
    graph->addNode(res);
    return res;
}

data_ptr_t StructNode::eval() {
    data_ptr_t data = Node::data();
    if (data->resolved()) {
        return data;
    } else {
        data_vec_t resVec;
        for (auto node : inputs_) {
            const data_ptr_t &data = node->data();
            assert(data, "Input data is null.");
            assert(data->resolved(), "Input data is not resolved.");
            resVec.push_back(data);
        }
        data->resolve(resVec);
    }
    return data;
}

/*
FunctorNode
*/

FunctorNode::FunctorNode(graph_ptr_t graph, const func_ptr_t &func)
    : Node(NodeType::FUNCTOR, DataType(DataTypeEnum::RUNTIME_CONSTANT), graph), func_(func) {
    nodeType_ = NodeType::FUNCTOR;
    inputs_.resize(3, nullptr);
}

inline shared_ptr<ParamsData> inputToParams(const node_ptr_t &node, const type_ptr_t &type) {
    data_ptr_t data = node->data();

    if (node->refCnt() == 0) {
        // if the node is a temporary created node
        TypeCode code = data->type()->code();
        switch (code) {
            // if the data is not a tuple, list, vector, or array, wrap it in a tuple
        case TypeCode::TUPLE:
            [[fallthrough]];
        case TypeCode::LIST:
            [[fallthrough]];
        case TypeCode::VECTOR:
            [[fallthrough]];
        case TypeCode::ARRAY:
            [[fallthrough]];
        case TypeCode::PARAMS:
            break;
        default:
            data = make_shared<TupleData>(data_list_t{data});
            break;
        }
    } else {
        data = make_shared<TupleData>(data_list_t{data});
    }

    // TODO: should we convert type in place here?
    data = data->convert(type, false);

    return dynamic_pointer_cast<ParamsData>(data);
}

void FunctorNode::fulfill() {
    const auto &withNode = inputs_[0];
    const auto &linkNode = inputs_[1];

    assert(withNode, "With node is not set.");
    assert(linkNode, "Link node is not set.");

    FunctorType *func = dynamic_cast<FunctorType *>(func_->type().get());

    auto withData = inputToParams(withNode, func->withType());
    auto linkData = inputToParams(linkNode, func->linkType());

    data_vec_t params;
    for (const auto &e : withData->indexData()) {
        params.push_back(e);
    }
    for (const auto &e : linkData->indexData()) {
        params.push_back(e);
    }

    func_->graph()->fulfill(params);
}

data_ptr_t FunctorNode::eval() {}

/*
OperatorNode
*/

OperatorNode::OperatorNode(graph_ptr_t graph, Operator *op)
    : Node(NodeType::OPERATOR, DataType(DataTypeEnum::RUNTIME_CONSTANT), graph), operator_(op) {}
