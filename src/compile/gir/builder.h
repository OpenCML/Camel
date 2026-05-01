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
 * Updated: May. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <fstream>
#include <iostream>

#include "camel/common/scope.h"
#include "camel/core/context/context.h"
#include "camel/core/error/abort.h"
#include "camel/core/module/module.h"

#include "camel/compile/gct.h"
#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/core/error/diagnostics.h"

namespace camel::compile::gir {

using void_ptr_t   = void *;
using Type         = camel::core::type::Type;
using FunctionType = camel::core::type::FunctionType;

using compile_graph_ptr_t   = std::shared_ptr<DraftGraphBuilder>;
using node_handle_t         = draft_node_t *;
using node_scope_t          = Scope<std::string, node_handle_t>;
using node_scope_ptr_t      = std::shared_ptr<node_scope_t>;
using graph_set_t           = std::vector<compile_graph_ptr_t>;
using graph_set_ptr_t       = std::shared_ptr<graph_set_t>;
using graph_scope_t         = Scope<std::string, graph_set_ptr_t>;
using graph_scope_ptr_t     = std::shared_ptr<graph_scope_t>;
using decorated_scope_t     = Scope<std::string, compile_graph_ptr_t>;
using decorated_scope_ptr_t = std::shared_ptr<decorated_scope_t>;

class Builder {
  public:
    Builder(
        const camel::core::context::context_ptr_t &context,
        const camel::core::module::module_ptr_t &module)
        : context_(context), module_(module) {}

    compile_graph_ptr_t build(GCT::node_ptr_t &gct, camel::core::error::diagnostics_ptr_t diags);

    compile_graph_ptr_t rootGraph() const { return rootGraph_; }

  private:
    compile_graph_ptr_t rootGraph_;
    compile_graph_ptr_t currGraph_;

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
    using node_modifier_map_t = std::unordered_map<draft_node_ref_t, draft_node_ref_t>;

    std::unordered_set<DraftGraphBuilder *> usedGraphs_;
    std::unordered_map<DraftGraphBuilder *, node_modifier_map_t> nodeModifierMaps_;
    node_handle_t lastSyncedNode_{};
    size_t syntheticRefIndex_ = 0;

    std::optional<node_handle_t> nodeAt(const std::string &name) {
        EXEC_WHEN_DEBUG({
            std::stringstream ss;
            nodeScope_->dump(
                ss,
                [](std::ostream &os, const std::string &key, const node_handle_t &value) {
                    os << "[" << key << "] ";
                    if (value == nullptr) {
                        os << "<invalid>";
                        return;
                    }
                    auto *graph = DraftGraphBuilder::fromDraft(value->header.owner);
                    os << (graph ? graph->name() : std::string{"<detached>"}) << "#"
                       << value->header.selfId;
                });
            CAMEL_LOG_DEBUG_S("GIR Builder", "Accessing node '{}' from scope {}", name, ss.str());
        });
        return nodeScope_->get(name);
    }
    std::optional<graph_set_ptr_t> graphsAt(const std::string &name) {
        EXEC_WHEN_DEBUG({
            std::stringstream ss;
            graphScope_->dump(
                ss,
                [&](std::ostream &os,
                    const std::string &key,
                    const std::shared_ptr<graph_vec_t> &value) {
                    os << "[" << key << "]";
                    for (const auto &graph : *value) {
                        os << " " << (graph ? graph->name() : std::string{"<null>"});
                    }
                });
            CAMEL_LOG_DEBUG_S("GIR Builder", "Accessing graph '{}' from scope {}", name, ss.str());
        });
        return graphScope_->get(name);
    }
    std::optional<compile_graph_ptr_t> decoratedGraphAt(const std::string &name) {
        return decoratedScope_ ? decoratedScope_->get(name) : std::nullopt;
    }

    bool insertNode(const std::string &name, node_handle_t node);
    bool insertGraph(const std::string &name, const compile_graph_ptr_t &graph);
    bool insertDecoratedGraph(const std::string &name, const compile_graph_ptr_t &graph);

    compile_graph_ptr_t enterScope(FunctionType *funcType, const std::string &name = "");
    void leaveScope();

    node_handle_t createFuncDataNode(
        const compile_graph_ptr_t &graph, bool getCallableNode, bool allowParameterization);
    std::optional<node_handle_t> modifierOf(node_handle_t node) const;
    void setModifier(node_handle_t input, node_handle_t modifier);
    compile_graph_ptr_t buildDecoratedGraph(
        const std::string &funcName, const compile_graph_ptr_t &rawGraph,
        const std::vector<GCT::node_ptr_t> &annoNodes);
    node_handle_t applyDecoratorAnno(const GCT::node_ptr_t &annoNode, node_handle_t funcValueNode);
    node_handle_t resolveCrossGraphRef(node_handle_t node, const std::string &name);
    node_handle_t resolveNodeByRef(const std::string &name);

    std::any visit(const GCT::node_ptr_t &gct);

    void_ptr_t visitDeclNode(const GCT::node_ptr_t &gct);
    compile_graph_ptr_t visitFuncNode(const GCT::node_ptr_t &gct);
    node_handle_t visitDataNode(const GCT::node_ptr_t &gct);
    Type *visitTypeNode(const GCT::node_ptr_t &gct);
    node_handle_t visitNRefNode(const GCT::node_ptr_t &gct);
    node_handle_t visitDRefNode(const GCT::node_ptr_t &gct);
    node_handle_t visitCastNode(const GCT::node_ptr_t &gct);
    node_handle_t visitVariNode(const GCT::node_ptr_t &gct);
    node_handle_t visitWaitNode(const GCT::node_ptr_t &gct);
    node_handle_t visitLinkNode(const GCT::node_ptr_t &gct);
    node_handle_t visitWithNode(const GCT::node_ptr_t &gct);
    node_handle_t visitAccsNode(const GCT::node_ptr_t &gct);
    node_handle_t visitBrchNode(const GCT::node_ptr_t &gct);
    node_handle_t visitAnnoNode(const GCT::node_ptr_t &gct);
    node_handle_t visitExitNode(const GCT::node_ptr_t &gct);
    node_handle_t visitExecNode(const GCT::node_ptr_t &gct);
    void_ptr_t visitExptNode(const GCT::node_ptr_t &gct);
};

} // namespace camel::compile::gir
