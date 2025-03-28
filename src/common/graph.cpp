/**
 * Copyright (c) 2024 the OpenCML Organization
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
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "graph.h"
#include "utils/log.h"

using namespace std;
using namespace GIR;

ostream &GIR::operator<<(ostream &os, NodeType type) {
    switch (type) {
    case NodeType::GRAPH:
        os << "GRAPH";
        break;
    case NodeType::DATA:
        os << "DATA";
        break;
    case NodeType::STRUCT:
        os << "STRUCT";
        break;
    case NodeType::SELECT:
        os << "SELECT";
        break;
    case NodeType::FUNCTOR:
        os << "FUNCTOR";
        break;
    case NodeType::OPERATOR:
        os << "OPERATOR";
        break;
    }
    return os;
}

ostream &GIR::operator<<(ostream &os, DataTypeEnum type) {
    switch (type) {
    case DataTypeEnum::SHARED_CONSTANT:
        os << "SHARED_CONSTANT";
        break;
    case DataTypeEnum::SHARED_VARIABLE:
        os << "SHARED_VARIABLE";
        break;
    case DataTypeEnum::RUNTIME_CONSTANT:
        os << "RUNTIME_CONSTANT";
        break;
    case DataTypeEnum::RUNTIME_VARIABLE:
        os << "RUNTIME_VARIABLE";
        break;
    }
    return os;
}

/*
Node
*/

data_ptr_t Node::data() const {
    const auto wg = graph_.lock();
    cml_assert(wg, "Graph is not set.");
    if (dataType_.variable) {
        return wg->getVariable(dataIndex_);
    } else {
        return wg->getConstant(dataIndex_);
    }
}

void Node::makeVariable(bool shared) {
    const auto wg = graph_.lock();
    cml_assert(wg, "Graph is not set for Node.");
    cml_assert(!dataType_.variable, "Node is already a variable.");
    dataIndex_ = wg->makeVariable(dataIndex_, shared);
    dataType_.shared = shared;
}

/*
Graph
*/

GIR::Graph::Graph()
    : Node(NodeType::GRAPH, DataType{}), nodes_(make_shared<node_vec_t>()),
      ports_(make_shared<vector<tuple<size_t, size_t, bool>>>()), subGraphs_(),
      sharedConstants_(make_shared<data_vec_t>()), sharedVariables_(make_shared<data_vec_t>()), runtimeConstants_(),
      rtVariableIndices_(make_shared<vector<InitIndex>>()), runtimeVariables_() {}

GIR::Graph::Graph(Graph &other)
    : Node(NodeType::GRAPH, DataType{}), nodes_(other.nodes_), ports_(other.ports_), subGraphs_(other.subGraphs_),
      sharedConstants_(other.sharedConstants_), sharedVariables_(other.sharedVariables_),
      runtimeConstants_(other.runtimeConstants_.size()), rtVariableIndices_(other.rtVariableIndices_),
      runtimeVariables_() {
    for (const auto &g : other.subGraphs_) {
        subGraphs_.push_back(make_shared<Graph>(*g));
    }
    for (const auto &idx : *rtVariableIndices_) {
        runtimeVariables_.push_back(idx);
    }
}

graph_ptr_t GIR::Graph::create(graph_ptr_t graph) {
    const auto res = make_shared<Graph>();
    res->graph_ = graph;
    if (graph) {
        graph->addSubGraph(res);
    }
    return res;
}

void GIR::Graph::setFuncType(const func_type_ptr_t &type) { funcType_ = type; }

func_type_ptr_t GIR::Graph::funcType() const {
    if (funcType_ == nullptr) {
        throw runtime_error("This graph has not been set to a functor.");
    }
    return funcType_;
}

void GIR::Graph::addNode(const node_ptr_t &node) { nodes_->push_back(node); }

node_ptr_t GIR::Graph::addPort(bool isVar) {
    node_ptr_t node = DataNode::create(dynamic_pointer_cast<Graph>(shared_from_this()), make_shared<NullData>(), false);
    if (isVar) {
        node->makeVariable();
    }
    size_t idx = node->index();
    ports_->push_back({nodes_->size() - 1, idx, isVar});
    return node;
}

void GIR::Graph::addSubGraph(const graph_ptr_t &graph) {
    // here we assume that the subgraph is a new blank graph
    subGraphs_.push_back(graph);
}

void GIR::Graph::setOutput(const node_ptr_t &node) { output_ = node; }

size_t GIR::Graph::makeVariable(size_t index, bool shared) {
    if (shared) {
        cml_assert(!dataType_.shared, "Cannot make a shared variable from a runtime constant.");
        sharedVariables_->push_back(sharedConstants_->at(index));
        return sharedVariables_->size() - 1;
    } else {
        InitIndex idx{index, false};
        rtVariableIndices_->push_back(idx);
        runtimeVariables_.push_back(idx);
        return runtimeVariables_.size() - 1;
    }
}

size_t GIR::Graph::addConstant(const data_ptr_t &data, bool shared) {
    if (shared) {
        sharedConstants_->push_back(data);
        return sharedConstants_->size() - 1;
    } else {
        runtimeConstants_.push_back(data);
        return runtimeConstants_.size() - 1;
    }
}

data_ptr_t GIR::Graph::getConstant(size_t index, bool shared) {
    if (shared) {
        cml_assert(index < sharedConstants_->size(), "Constant index out of range.");
        return sharedConstants_->at(index);
    } else {
        cml_assert(index < runtimeConstants_.size(), "Constant index out of range.");
        return runtimeConstants_.at(index);
    }
}

data_ptr_t GIR::Graph::getVariable(size_t index, bool shared) {
    if (shared) {
        cml_assert(index < sharedVariables_->size(), "Variable index out of range.");
        return sharedVariables_->at(index);
    } else {
        cml_assert(index < runtimeVariables_.size(), "Variable index out of range.");
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

void GIR::Graph::setConstant(size_t index, const data_ptr_t &data, bool shared) {
    if (shared) {
        cml_assert(index < sharedConstants_->size(), "Constant index out of range.");
        sharedConstants_->at(index) = data;
    } else {
        cml_assert(index < runtimeConstants_.size(), "Constant index out of range.");
        runtimeConstants_.at(index) = data;
    }
}

void GIR::Graph::setVariable(size_t index, const data_ptr_t &data, bool shared) {
    if (shared) {
        cml_assert(index < sharedVariables_->size(), "Variable index out of range.");
        sharedVariables_->at(index) = data;
    } else {
        cml_assert(index < runtimeVariables_.size(), "Variable index out of range.");
        runtimeVariables_.at(index) = data;
    }
}

void GIR::Graph::fulfill(const data_vec_t &dataList) {
    cml_assert(dataList.size() == ports_->size(), "Data list size does not match ports size.");
    for (size_t i = 0; i < dataList.size(); i++) {
        const auto &data = dataList[i];
        const auto &[_, dataIndex, isVar] = ports_->at(i);
        if (isVar) {
            setVariable(dataIndex, data, false);
        } else {
            setConstant(dataIndex, data, false);
        }
    }
}

data_ptr_t GIR::Graph::eval() { return nullptr; }

/*
DataNode
*/

DataNode::DataNode(graph_ptr_t graph, const data_ptr_t &data, bool shared)
    : Node(NodeType::DATA, DataType{shared, false}, graph) {
    cml_assert(graph, "Graph is not set for DataNode.");
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
    cml_assert(graph, "Graph is not set for StructNode.");
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
            cml_assert(data, "Input data is null.");
            cml_assert(data->resolved(), "Input data is not resolved.");
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
    inputs_.resize(2, nullptr);
}

node_ptr_t FunctorNode::create(graph_ptr_t graph, const func_ptr_t &func) {
    const auto res = make_shared<FunctorNode>(graph, func);
    graph->addNode(res);
    return res;
}

func_ptr_t FunctorNode::func() const { return func_; }

func_type_ptr_t FunctorNode::type() const { return dynamic_pointer_cast<FunctorType>(func_->type()); }

func_node_ptr_t FunctorNode::copyTo(graph_ptr_t graph) const {
    func_node_ptr_t node = make_shared<FunctorNode>(graph, dynamic_pointer_cast<FunctorData>(func_->clone()));
    graph->addNode(node);
    return node;
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

    cml_assert(withNode, "With node is not set.");
    cml_assert(linkNode, "Link node is not set.");

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

data_ptr_t FunctorNode::eval() { return nullptr; }

/*
OperatorNode
*/

OperatorNode::OperatorNode(graph_ptr_t graph, Operator op)
    : Node(NodeType::OPERATOR, DataType(DataTypeEnum::RUNTIME_CONSTANT), graph), operator_(op) {
    inputs_.resize(2, nullptr);
}

node_ptr_t OperatorNode::create(graph_ptr_t graph, Operator op) {
    const auto res = make_shared<OperatorNode>(graph, op);
    graph->addNode(res);
    return res;
}

/*
SelectNode
*/

SelectNode::SelectNode(graph_ptr_t graph, const func_vec_t &cases)
    : Node(NodeType::SELECT, DataType(DataTypeEnum::RUNTIME_CONSTANT), graph), funcs_(make_shared<func_vec_t>(cases)) {}

SelectNode::SelectNode(graph_ptr_t graph, const oper_vec_t &cases)
    : Node(NodeType::SELECT, DataType(DataTypeEnum::RUNTIME_CONSTANT), graph), opers_(make_shared<oper_vec_t>(cases)) {}

node_ptr_t SelectNode::create(graph_ptr_t graph, const func_vec_t &cases) {
    const auto res = make_shared<SelectNode>(graph, cases);
    // temporary node, not added to graph
    return res;
}

node_ptr_t SelectNode::create(graph_ptr_t graph, const oper_vec_t &cases) {
    const auto res = make_shared<SelectNode>(graph, cases);
    // temporary node, not added to graph
    return res;
}

vector<func_type_ptr_t> SelectNode::types() const {
    vector<func_type_ptr_t> res;
    if (funcs_) {
        for (const auto &func : *funcs_) {
            res.push_back(dynamic_pointer_cast<FunctorType>(func->type()));
        }
    } else {
        for (const auto &oper : *opers_) {
            res.push_back(oper->type());
        }
    }
    return res;
}

node_ptr_t SelectNode::select(size_t index) {
    if (funcs_) {
        return FunctorNode::create(graph_.lock(), funcs_->at(index));
    } else {
        return OperatorNode::create(graph_.lock(), *opers_->at(index));
    }
}
