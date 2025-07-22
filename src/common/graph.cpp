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
    case NodeType::Literal:
        return "Literal";
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

void Graph::setFuncType(const func_type_ptr_t &type) { funcType_ = type; }

func_type_ptr_t Graph::funcType() const {
    if (funcType_ == nullptr) {
        throw runtime_error("This graph has not been set to a functor.");
    }
    return funcType_;
}

void Graph::addNode(const node_ptr_t &node) { nodes_.push_back(node); }

node_ptr_t Graph::addPort(bool isVar) {}

void Graph::addSubGraph(const graph_ptr_t &graph) {
    // here we assume that the subgraph is a new blank graph
    subGraphs_.push_back(graph);
}

void Graph::setOutput(const node_ptr_t &node) { output_ = node; }

/*
LiteralNode
*/


/*
StructNode
*/

data_ptr_t StructNode::eval() {
    data_ptr_t data = Node::data();
    if (data->resolved()) {
        return data;
    } else {
        data_vec_t resVec;
        for (auto node : normInputs_) {
            const data_ptr_t &data = node->data();
            ASSERT(data, "Input data is null.");
            ASSERT(data->resolved(), "Input data is not resolved.");
            resVec.push_back(data);
        }
        data->resolve(resVec);
    }
    return data;
}

/*
OperatorNode
*/

/*
SelectNode
*/

} // namespace GraphIntermediateRepresentation
