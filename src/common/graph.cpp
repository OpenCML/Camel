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

namespace GraphIntermediateRepresentation {

std::string to_string(NodeType type) {
    switch (type) {
    case NodeType::Select:
        return "Select";
    case NodeType::Access:
        return "Access";
    case NodeType::Struct:
        return "Struct";
    case NodeType::Source:
        return "Source";
    case NodeType::Operator:
        return "Operator";
    case NodeType::Function:
        return "Function";
    }
    ASSERT(false, "Unknown NodeType");
    return "Unknown";
}

/*
Node
*/

/*
Graph
*/

void Graph::setFuncType(const func_type_ptr_t &type) {
    ASSERT(funcType_ == nullptr, "Function type has already been set.");
    funcType_ = type;
}

func_type_ptr_t Graph::funcType() const {
    ASSERT(funcType_ != nullptr, "Graph has not been set to a function type.");
    return funcType_;
}

void Graph::addNode(const node_ptr_t &node) { nodes_.push_back(node); }

node_ptr_t Graph::addPort() {
    DataIndex index = arena_->addConstant(nullptr, false);
    ports_.push_back(index);
    node_ptr_t portNode = SourceNode::create(shared_from_this(), index);
    addNode(portNode);
    return portNode;
}

void Graph::addSubGraph(const graph_ptr_t &graph) {
    // here we assume that the subgraph is a new blank graph
    subGraphs_.push_back(graph);
}

void Graph::setOutput(const node_ptr_t &node) {
    ASSERT(output_ == nullptr, "Output node has already been set.");
    output_ = node;
}

/*
LiteralNode
*/

/*
StructNode
*/

/*
OperatorNode
*/

/*
SelectNode
*/

} // namespace GraphIntermediateRepresentation
