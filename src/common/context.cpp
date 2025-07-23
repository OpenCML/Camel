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
 * Created: Aug. 18, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "context.h"
#include <set>

using namespace std;

Context::Context()
    : rootGraph_(GIR::Graph::create()), nodeScope_(node_scope_t::create()), graphScope_(graph_scope_t::create()),
      opScope_(operator_scope_t::create(globalOperators)) {
    currGraph_ = rootGraph_;
}

GIR::graph_ptr_t Context::pushScope(const std::string &name) {
    nodeScope_ = nodeScope_->push();
    graphScope_ = graphScope_->push();
    opScope_ = opScope_->push();
    currGraph_ = GIR::Graph::create(currGraph_, name);
    if (!name.empty()) { // avoid inserting anonymous graphs
        insertGraph(name, currGraph_);
    }
    return currGraph_;
}

void Context::popScope() {
    nodeScope_ = nodeScope_->pop();
    graphScope_ = graphScope_->pop();
    opScope_ = opScope_->pop();
    currGraph_ = currGraph_->outer();
}

bool Context::insertNode(const std::string &name, const GIR::node_ptr_t &node) {
    if (nodeScope_->has(name, false)) {
        return false;
    }
    nodeScope_->insert(name, {node, nullptr});
    return true;
}

bool Context::insertGraph(const std::string &name, const GIR::graph_ptr_t &graph) {
    if (graphScope_->has(name, false)) {
        auto graphs = graphScope_->at(name).value();
        // TODO: check if the graph is already in the list
        graphs->push_back(graph);
    }
    graphScope_->insert(name, std::make_shared<GIR::graph_vec_t>(1, graph));
    return true;
}

bool Context::insertOperator(const std::string &name, const operator_ptr_t &op) {
    if (opScope_->has(name, false)) {
        auto ops = opScope_->at(name).value();
        // TODO: check if the operator is already in the list
        ops->push_back(op);
    }
    opScope_->insert(name, std::make_shared<operator_vec_t>(1, op));
    return true;
}
