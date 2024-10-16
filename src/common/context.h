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
 * Updated: Oct. 16, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>

#include "graph.h"
#include "operator.h"
#include "scope.h"

using node_scope_t = Scope<std::string, gir::node_ptr_t>;
using node_scope_ptr_t = scope_ptr_t<std::string, gir::node_ptr_t>;
using operator_scope_t = Scope<std::string, Operator>;
using operator_scope_ptr_t = scope_ptr_t<std::string, Operator>;

class Context {
    gir::graph_ptr_t rootGraph_;
    gir::graph_ptr_t currGraph_;
    node_scope_ptr_t scope_;
    operator_scope_ptr_t opScope_;
    // gir::func_node_ptr_t currFunc_;

  public:
    Context();
    virtual ~Context() = default;

    node_scope_t &scope() { return *scope_; }
    operator_scope_t &opScope() { return *opScope_; }
    gir::graph_ptr_t &graph() { return currGraph_; }
    // gir::func_node_ptr_t &func() { return currFunc_; }

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
        if (opNode) {
            return *opNode;
        }
        auto opOp = opScope_->at(name);
        if (opOp) {
            return gir::OperatorNode::create(currGraph_, *opOp);
        }
        return std::nullopt;
    }

    void insert(const std::string &name, const gir::node_ptr_t &node) { scope_->insert(name, node); }
};

using context_ptr_t = std::shared_ptr<Context>;
