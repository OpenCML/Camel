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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <fstream>
#include <iostream>

#include "camel/common/scope.h"
#include "camel/core/context/context.h"
#include "camel/core/error/abort.h"
#include "camel/core/module/module.h"

#include "camel/compile/gct.h"
#include "camel/compile/gir.h"
#include "camel/core/error/diagnostics.h"

namespace camel::compile::gir {

using void_ptr_t = void *;

using node_scope_t          = Scope<std::string, Node *>;
using node_scope_ptr_t      = std::shared_ptr<node_scope_t>;
using graph_scope_t         = Scope<std::string, std::shared_ptr<graph_vec_t>>;
using graph_scope_ptr_t     = std::shared_ptr<graph_scope_t>;
using decorated_scope_t     = Scope<std::string, graph_ptr_t>;
using decorated_scope_ptr_t = std::shared_ptr<decorated_scope_t>;

class Builder {
  public:
    Builder(
        const camel::core::context::context_ptr_t &context,
        const camel::core::module::module_ptr_t &module)
        : context_(context), module_(module) {}

    graph_ptr_t build(GCT::node_ptr_t &gct, camel::core::error::diagnostics_ptr_t diags);

    graph_ptr_t rootGraph() const { return rootGraph_; }

  private:
    graph_ptr_t rootGraph_;
    graph_ptr_t currGraph_;

    node_scope_ptr_t nodeScope_;
    graph_scope_ptr_t graphScope_;
    decorated_scope_ptr_t decoratedScope_;

    camel::core::context::context_ptr_t context_;
    camel::core::module::module_ptr_t module_;
    camel::core::error::diagnostics_ptr_t diags_;

    bool waited_;
    bool synced_;
    bool varied_;

    // Track whether this Graph has already been used (via createFuncDataNode).
    // A used Graph can no longer accept additional closure captures.
    std::unordered_set<Graph *> usedGraphs_;
    std::unordered_map<Node *, Node *> nodeModifierMap_;
    Node *lastSyncedNode_     = nullptr;
    size_t syntheticRefIndex_ = 0;

    std::optional<Node *> nodeAt(const std::string &name) {
        EXEC_WHEN_DEBUG({
            std::stringstream ss;
            nodeScope_->dump(ss);
            CAMEL_LOG_DEBUG_S("GIR Builder", "Accessing node '{}' from scope {}", name, ss.str());
        });
        return nodeScope_->get(name);
    }
    std::optional<std::shared_ptr<graph_vec_t>> graphsAt(const std::string &name) {
        EXEC_WHEN_DEBUG({
            std::stringstream ss;
            graphScope_->dump(
                ss,
                [&](std::ostream &os,
                    const std::string &key,
                    const std::shared_ptr<graph_vec_t> &value) {
                    os << "[" << key << "]";
                    for (const auto &graph : *value) {
                        os << graph->toString() << " ";
                    }
                });
            CAMEL_LOG_DEBUG_S("GIR Builder", "Accessing graph '{}' from scope {}", name, ss.str());
        });
        return graphScope_->get(name);
    }
    std::optional<graph_ptr_t> decoratedGraphAt(const std::string &name) {
        return decoratedScope_ ? decoratedScope_->get(name) : std::nullopt;
    }

    bool insertNode(const std::string &name, Node *node);
    bool insertGraph(const std::string &name, const graph_ptr_t &graph);
    bool insertDecoratedGraph(const std::string &name, const graph_ptr_t &graph);

    graph_ptr_t enterScope(FunctionType *funcType, const std::string &name = "");
    void leaveScope();

    Node *
    createFuncDataNode(const graph_ptr_t &graph, bool getCallableNode, bool allowParameterization);
    graph_ptr_t buildDecoratedGraph(
        const std::string &funcName, const graph_ptr_t &rawGraph,
        const std::vector<GCT::node_ptr_t> &annoNodes);
    Node *applyDecoratorAnno(const GCT::node_ptr_t &annoNode, Node *funcValueNode);
    Node *resolveCrossGraphRef(Node *node, const std::string &name);
    Node *resolveNodeByRef(const std::string &name);

    std::any visit(const GCT::node_ptr_t &gct);

    void_ptr_t visitDeclNode(const GCT::node_ptr_t &gct);
    graph_ptr_t visitFuncNode(const GCT::node_ptr_t &gct);
    Node *visitDataNode(const GCT::node_ptr_t &gct);
    Type *visitTypeNode(const GCT::node_ptr_t &gct);
    Node *visitNRefNode(const GCT::node_ptr_t &gct);
    Node *visitDRefNode(const GCT::node_ptr_t &gct);
    Node *visitCastNode(const GCT::node_ptr_t &gct);
    Node *visitVariNode(const GCT::node_ptr_t &gct);
    Node *visitWaitNode(const GCT::node_ptr_t &gct);
    Node *visitLinkNode(const GCT::node_ptr_t &gct);
    Node *visitWithNode(const GCT::node_ptr_t &gct);
    Node *visitAccsNode(const GCT::node_ptr_t &gct);
    Node *visitBrchNode(const GCT::node_ptr_t &gct);
    Node *visitAnnoNode(const GCT::node_ptr_t &gct);
    Node *visitExitNode(const GCT::node_ptr_t &gct);
    Node *visitExecNode(const GCT::node_ptr_t &gct);
    void_ptr_t visitExptNode(const GCT::node_ptr_t &gct);
};

} // namespace camel::compile::gir
