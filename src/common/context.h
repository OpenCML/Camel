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

#pragma once

#include <iostream>
#include <string>

#include "graph.h"
#include "operator.h"
#include "scope.h"

using node_scope_t = Scope<std::string, GIR::node_ptr_t>;
using node_scope_ptr_t = scope_ptr_t<std::string, GIR::node_ptr_t>;
using func_scope_t = Scope<std::string, std::shared_ptr<func_vec_t>>;
using func_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<func_vec_t>>;
using operator_scope_t = Scope<std::string, std::shared_ptr<oper_vec_t>>;
using operator_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<oper_vec_t>>;

class Context {
    GIR::graph_ptr_t rootGraph_;
    GIR::graph_ptr_t currGraph_;
    node_scope_ptr_t nodeScope_;
    func_scope_ptr_t funcScope_;
    operator_scope_ptr_t opScope_;

    std::unordered_map<func_type_ptr_t,
                       std::tuple<node_scope_ptr_t, func_scope_ptr_t, operator_scope_ptr_t, GIR::graph_ptr_t>>
        funcCache_;

    // only generated when getNodeIdent() is called to save memory
    std::unordered_map<GIR::node_ptr_t, std::string> nodeIdentsMap_;
    void generateNodeIdentsMap();

  public:
    Context();
    virtual ~Context() = default;

    node_scope_t &nodeScope() { return *nodeScope_; }
    func_scope_t &funcScope() { return *funcScope_; }
    operator_scope_t &opScope() { return *opScope_; }

    GIR::graph_ptr_t &rootGraph() { return rootGraph_; }
    GIR::graph_ptr_t &currGraph() { return currGraph_; }

    std::optional<std::string> getNodeIdent(const GIR::node_ptr_t &node);

    void pushScope(func_type_ptr_t key);
    void popScope(func_type_ptr_t key = nullptr);

    bool cached(func_type_ptr_t key) { return funcCache_.find(key) != funcCache_.end(); }

    std::optional<GIR::node_ptr_t> nodeAt(const std::string &name);

    bool insertNode(const std::string &name, const GIR::node_ptr_t &node);
    bool insertFunc(const std::string &name, func_ptr_t func);
    bool insertOperator(const std::string &name, const oper_ptr_t &op);
};

using context_ptr_t = std::shared_ptr<Context>;
