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
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>
#include <string>

#include "graph.h"
#include "operator.h"
#include "scope.h"

using node_scope_t = Scope<std::string, gir::node_ptr_t>;
using node_scope_ptr_t = scope_ptr_t<std::string, gir::node_ptr_t>;
using func_scope_t = Scope<std::string, std::shared_ptr<gir::node_vec_t>>;
using func_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<gir::node_vec_t>>;
using operator_scope_t = Scope<std::string, std::shared_ptr<std::vector<operator_ptr_t>>>;
using operator_scope_ptr_t = scope_ptr_t<std::string, std::shared_ptr<std::vector<operator_ptr_t>>>;

class Context {
    gir::graph_ptr_t rootGraph_;
    gir::graph_ptr_t currGraph_;
    node_scope_ptr_t nodeScope_;
    func_scope_ptr_t funcScope_;
    operator_scope_ptr_t opScope_;

    std::unordered_map<void *, std::tuple<node_scope_ptr_t, func_scope_ptr_t, operator_scope_ptr_t, gir::graph_ptr_t>>
        consCache_;
    std::unordered_map<void *, gir::node_ptr_t> nodeCache_;

    // only generated when getNodeIdent() is called to save memory
    std::unordered_map<gir::node_ptr_t, std::string> nodeIdentsMap_;
    void generateNodeIdentsMap();

  public:
    Context();
    virtual ~Context() = default;

    node_scope_t &nodeScope() { return *nodeScope_; }
    func_scope_t &funcScope() { return *funcScope_; }
    operator_scope_t &opScope() { return *opScope_; }

    gir::graph_ptr_t &rootGraph() { return rootGraph_; }
    gir::graph_ptr_t &currGraph() { return currGraph_; }

    std::optional<std::string> getNodeIdent(const gir::node_ptr_t &node);

    void pushScope(void *key);
    void popScope(void *key = nullptr);

    bool cached(void *key) { return consCache_.find(key) != consCache_.end(); }
    void cacheNode(void *key, const gir::node_ptr_t &node) { nodeCache_[key] = node; }
    void eraseCachedNode(void *key) { nodeCache_.erase(key); }
    gir::node_ptr_t getCachedNode(void *key) { return nodeCache_[key]; }

    std::optional<gir::node_ptr_t> nodeAt(const std::string &name);

    bool insertNode(const std::string &name, const gir::node_ptr_t &node);
    bool insertFunc(const std::string &name, const gir::node_ptr_t &node);
    bool insertOperator(const std::string &name, const operator_ptr_t &op);
};

using context_ptr_t = std::shared_ptr<Context>;
