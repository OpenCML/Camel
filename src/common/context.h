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

#pragma once

#include <iostream>
#include <string>
#include <unordered_map>

#include "graph.h"
#include "operator.h"
#include "scope.h"

using node_scope_t = Scope<std::string, std::pair<GIR::node_ptr_t, GIR::node_ptr_t>>;
using node_scope_ptr_t = scope_ptr_t<std::string, std::pair<GIR::node_ptr_t, GIR::node_ptr_t>>; // pair<node, modifier>
using graph_scope_t = Scope<std::string, std::shared_ptr<GIR::graph_vec_t>>;
using graph_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<GIR::graph_vec_t>>;
using operator_scope_t = Scope<std::string, std::shared_ptr<operator_vec_t>>;
using operator_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<operator_vec_t>>;

class Context {
    GIR::graph_ptr_t rootGraph_;
    GIR::graph_ptr_t currGraph_;

    node_scope_ptr_t nodeScope_;
    graph_scope_ptr_t graphScope_;
    operator_scope_ptr_t opScope_;

  public:
    Context();
    virtual ~Context() = default;

    node_scope_t &nodeScope() { return *nodeScope_; }
    graph_scope_t &graphScope() { return *graphScope_; }
    operator_scope_t &opScope() { return *opScope_; }

    GIR::graph_ptr_t &rootGraph() { return rootGraph_; }
    GIR::graph_ptr_t &currGraph() { return currGraph_; }

    GIR::graph_ptr_t enterScope(const std::string &name);
    void leaveScope();

    std::unordered_map<GIR::node_ptr_t, std::string> buildNodeIdentsMap() const;

    std::optional<std::pair<GIR::node_ptr_t, GIR::node_ptr_t>> nodeAt(const std::string &name) {
        return nodeScope_->get(name);
    }
    std::optional<std::shared_ptr<GIR::graph_vec_t>> graphAt(const std::string &name) { return graphScope_->get(name); }
    std::optional<std::shared_ptr<operator_vec_t>> operatorAt(const std::string &name) { return opScope_->get(name); }

    bool insertNode(const std::string &name, const GIR::node_ptr_t &node);
    bool insertGraph(const std::string &name, const GIR::graph_ptr_t &graph);
    bool insertOperator(const std::string &name, const operator_ptr_t &op);
};

using context_ptr_t = std::shared_ptr<Context>;
