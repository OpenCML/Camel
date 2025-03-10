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
 * Created: Aug. 18, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "context.h"
#include <set>

using namespace std;

Context::Context()
    : rootGraph_(GIR::Graph::create()), nodeScope_(node_scope_t::create()), funcScope_(func_scope_t::create()),
      opScope_(operator_scope_t::create(globalOperators)) {
    currGraph_ = rootGraph_;
    funcCache_[nullptr] = {nodeScope_, funcScope_, opScope_, currGraph_};
}

void Context::generateNodeIdentsMap() {
    multiset<string> idents;
    for (auto &[key, cache] : funcCache_) {
        auto &[scope, _, __, ___] = cache;
        for (auto &[k, node] : scope->self()) {
            idents.insert(k);
            string ident;
            if (idents.count(k) > 1) {
                ident = k + "_" + to_string(idents.count(k));
            } else {
                ident = k;
            }
            nodeIdentsMap_[node] = ident;
        }
    }
}

std::optional<std::string> Context::getNodeIdent(const GIR::node_ptr_t &node) {
    if (nodeIdentsMap_.empty()) {
        generateNodeIdentsMap();
    }
    if (nodeIdentsMap_.find(node) != nodeIdentsMap_.end()) {
        return nodeIdentsMap_[node];
    }
    return std::nullopt;
}

void Context::pushScope(func_type_ptr_t key) {
    if (funcCache_.find(key) != funcCache_.end()) {
        auto [nodeScope, funcScope, opScope, graph] = funcCache_[key];
        nodeScope_ = nodeScope;
        funcScope_ = funcScope;
        opScope_ = opScope;
        currGraph_ = graph;
    } else {
        nodeScope_ = nodeScope_->push();
        funcScope_ = funcScope_->push();
        opScope_ = opScope_->push();
        currGraph_ = GIR::Graph::create(currGraph_);
        funcCache_[key] = std::make_tuple(nodeScope_, funcScope_, opScope_, currGraph_);
    }
}

void Context::popScope(func_type_ptr_t key) {
    nodeScope_ = nodeScope_->pop();
    funcScope_ = funcScope_->pop();
    opScope_ = opScope_->pop();
    currGraph_ = currGraph_->outer();
    if (key != nullptr) {
        funcCache_.erase(key);
    }
}

std::optional<GIR::node_ptr_t> Context::nodeAt(const std::string &name) {
    auto opNode = nodeScope_->at(name);
    if (opNode.has_value()) {
        return opNode.value();
    }
    auto opFunc = funcScope_->at(name);
    if (opFunc.has_value()) {
        func_vec_t &funcs = *opFunc.value();
        return GIR::SelectNode::create(currGraph_, funcs);
    }
    auto opOp = opScope_->at(name);
    if (opOp.has_value()) {
        return GIR::SelectNode::create(currGraph_, *opOp.value());
    }
    return std::nullopt;
}

bool Context::insertNode(const std::string &name, const GIR::node_ptr_t &node) {
    if (nodeScope_->has(name, false)) {
        return false;
    }
    nodeScope_->insert(name, node);
    return true;
}

bool Context::insertFunc(const std::string &name, func_ptr_t func) {
    if (funcScope_->has(name, false)) {
        const auto funcs = funcScope_->at(name).value();
        // TODO: check if the func node is already in the list
        funcs->push_back(func);
    }
    funcScope_->insert(name, std::make_shared<func_vec_t>(1, func));
    return true;
}

bool Context::insertOperator(const std::string &name, const oper_ptr_t &op) {
    if (opScope_->has(name, false)) {
        const auto ops = opScope_->at(name).value();
        // TODO: check if the operator is already in the list
        ops->push_back(op);
    }
    opScope_->insert(name, std::make_shared<oper_vec_t>(1, op));
    return true;
}
