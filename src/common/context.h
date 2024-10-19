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
 * Updated: Oct. 19, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>
#include <iostream>

#include "graph.h"
#include "operator.h"
#include "scope.h"

using node_scope_t = Scope<std::string, std::shared_ptr<gir::node_vec_t>>;
using node_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<gir::node_vec_t>>;
using operator_scope_t = Scope<std::string, std::shared_ptr<std::vector<operator_ptr_t>>>;
using operator_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<std::vector<operator_ptr_t>>>;

class Context {
    gir::graph_ptr_t rootGraph_;
    gir::graph_ptr_t currGraph_;
    node_scope_ptr_t scope_;
    operator_scope_ptr_t opScope_;

  public:
    Context();
    virtual ~Context() = default;

    node_scope_t &scope() { return *scope_; }
    operator_scope_t &opScope() { return *opScope_; }
    gir::graph_ptr_t &graph() { return currGraph_; }

    void pushScope() {
        scope_ = scope_->push();
        opScope_ = opScope_->push();
        currGraph_ = gir::Graph::create(currGraph_);
    }

    void popScope() {
        scope_ = scope_->pop();
        opScope_ = opScope_->pop();
        currGraph_ = currGraph_->outer();
    }

    std::optional<gir::node_ptr_t> nodeAt(const std::string &name) {
        auto opNode = scope_->at(name);
        if (opNode.has_value()) {
            gir::node_vec_t &nodes = *opNode.value();
            if (nodes.size() == 1 && nodes[0]->type() != gir::NodeType::FUNCTOR) {
                return nodes[0];
            }
            return gir::SelectNode::create(currGraph_, nodes);
        }
        auto opOp = opScope_->at(name);
        if (opOp.has_value()) {
            return gir::SelectNode::create(currGraph_, *opOp.value());
        }
        return std::nullopt;
    }

    bool insertData(const std::string &name, const gir::node_ptr_t &node) {
        if (scope_->has(name)) {
            return false;
        }
        scope_->insert(name, std::make_shared<gir::node_vec_t>(1, node));
        return true;
    }

    bool insertFunc(const std::string &name, const gir::node_ptr_t &node) {
        if (scope_->has(name)) {
            const auto nodes = scope_->at(name).value();
            // TODO: check if the func node is already in the list
            nodes->push_back(node);
        }
        scope_->insert(name, std::make_shared<gir::node_vec_t>(1, node));
        return true;
    }

    bool insertOperator(const std::string &name, const operator_ptr_t &op) {
        if (opScope_->has(name)) {
            const auto ops = opScope_->at(name).value();
            // TODO: check if the operator is already in the list
            ops->push_back(op);
        }
        opScope_->insert(name, std::make_shared<std::vector<operator_ptr_t>>(1, op));
        return true;
    }
};

using context_ptr_t = std::shared_ptr<Context>;
