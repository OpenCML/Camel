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
 * Created: May. 29, 2024
 * Updated: Oct. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <fstream>
#include <iostream>

#include "common/scope.h"
#include "core/context/context.h"
#include "core/module/module.h"
#include "error/abort.h"

#include "../gct.h"
#include "../gir.h"

namespace GraphIR {

using void_ptr_t = void *;

using node_scope_t = Scope<std::string, node_ptr_t>;
using node_scope_ptr_t = std::shared_ptr<node_scope_t>;
using graph_scope_t = Scope<std::string, std::shared_ptr<graph_vec_t>>;
using graph_scope_ptr_t = std::shared_ptr<graph_scope_t>;

class Builder {
  public:
    Builder(const context_ptr_t &context, const module_ptr_t &module)
        : context_(context), module_(module) {
        nodeScope_ = node_scope_t::create();
        graphScope_ = graph_scope_t::create();
        rootGraph_ = Graph::create(nullptr, "__root__");
        rootGraph_->setFuncType(
            std::make_shared<FunctionType>(param_init_list_t{}, param_init_list_t{}, Type::Void()));
        currGraph_ = rootGraph_;
    }

    graph_ptr_t build(GCT::node_ptr_t &gct, diagnostics_ptr_t diags) {
        waited_ = false;
        synced_ = false;
        varied_ = false;
        diags_ = diags;
        visit(gct);
        return rootGraph_;
    }

    graph_ptr_t rootGraph() const { return rootGraph_; }

  private:
    graph_ptr_t rootGraph_;
    graph_ptr_t currGraph_;

    node_scope_ptr_t nodeScope_;
    graph_scope_ptr_t graphScope_;

    context_ptr_t context_;
    module_ptr_t module_;
    diagnostics_ptr_t diags_;

    bool waited_;
    bool synced_;
    bool varied_;

    std::unordered_map<Node *, node_wptr_t> nodeModifierMap_;
    node_ptr_t lastSyncedNode_;

    std::optional<node_ptr_t> nodeAt(const std::string &name) { return nodeScope_->get(name); }
    std::optional<std::shared_ptr<graph_vec_t>> graphsAt(const std::string &name) {
        return graphScope_->get(name);
    }

    bool insertNode(const std::string &name, const node_ptr_t &node);
    bool insertGraph(const std::string &name, const graph_ptr_t &graph);

    graph_ptr_t enterScope(const std::string &name = "");
    void leaveScope();

    node_ptr_t resolveNodeByRef(const std::string &name);
    void setGraphOutputAndExitType(const graph_ptr_t &graph, const node_ptr_t &node);

    std::any visit(const GCT::node_ptr_t &gct);

    void_ptr_t visitDeclNode(const GCT::node_ptr_t &gct);
    graph_ptr_t visitFuncNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitDataNode(const GCT::node_ptr_t &gct);
    type_ptr_t visitTypeNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitNRefNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitDRefNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitVariNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitWaitNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitLinkNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitWithNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitAccsNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitBrchNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitAnnoNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitExitNode(const GCT::node_ptr_t &gct);
    node_ptr_t visitExecNode(const GCT::node_ptr_t &gct);
    void_ptr_t visitExptNode(const GCT::node_ptr_t &gct);
};

} // namespace GraphIR
