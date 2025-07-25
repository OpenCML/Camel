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
#include "builtin/operators/init.h"

#include <set>

using namespace std;

Context::Context()
    : rootGraph_(GIR::Graph::create()), nodeScope_(node_scope_t::create()), graphScope_(graph_scope_t::create()) {
    initGlobalOperators(); // Initialize global operators
    // for (const auto &pair : globalOperators) {
    //     const auto &name = pair.first;
    //     const auto &ops = pair.second;
    //     std::cout << "Registered global operator: " << name << " with " << ops->size() << " variants." << std::endl;
    // }
    opScope_ = operator_scope_t::create(globalOperators);
    currGraph_ = rootGraph_;
}

GIR::graph_ptr_t Context::enterScope(const std::string &name) {
    if (name.empty()) {
        currGraph_ = GIR::Graph::create(currGraph_);
    } else {
        auto graphs = graphScope_->get(name);
        if (graphs.has_value() && !graphs.value()->empty()) {
            currGraph_ = graphs.value()->front();
        } else {
            currGraph_ = GIR::Graph::create(currGraph_, name);
            insertGraph(name, currGraph_);
        }
    }
    nodeScope_ = nodeScope_->enter(name);
    graphScope_ = graphScope_->enter(name);
    opScope_ = opScope_->enter(name);
    return currGraph_;
}

void Context::leaveScope() {
    nodeScope_ = nodeScope_->leave();
    graphScope_ = graphScope_->leave();
    opScope_ = opScope_->leave();
    currGraph_ = currGraph_->outer();
}

std::unordered_map<GIR::node_ptr_t, std::string> Context::buildNodeIdentsMap() const {
    std::unordered_map<GIR::node_ptr_t, std::string> identsMap;
    auto visit = [&identsMap](auto self, node_scope_ptr_t scope) -> void {
        for (const auto &pair : scope->map()) {
            const auto &name = pair.first;
            const auto &nodePair = pair.second;
            if (nodePair.first) {
                identsMap[nodePair.first] = name;
            }
        }
        for (const auto &innerScope : scope->innerScopes()) {
            self(self, innerScope);
        }
    };
    visit(visit, nodeScope_); // Recursive lambda to visit all inner scopes
    return identsMap;
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
        auto graphs = graphScope_->get(name).value();
        // TODO: check if the graph is already in the list
        graphs->push_back(graph);
    }
    graphScope_->insert(name, std::make_shared<GIR::graph_vec_t>(1, graph));
    return true;
}

bool Context::insertOperator(const std::string &name, const operator_ptr_t &op) {
    if (opScope_->has(name, false)) {
        auto ops = opScope_->get(name).value();
        // TODO: check if the operator is already in the list
        ops->push_back(op);
    }
    opScope_->insert(name, std::make_shared<operator_vec_t>(1, op));
    return true;
}
