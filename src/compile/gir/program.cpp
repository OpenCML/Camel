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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The program-wide side of the GIR builder: one builder per program, the modules it builds
 * into one root graph, and the link step that runs once a module is built (design principles
 * 2.11 "模块"; docs/design/graph-model/README.cn.md 3.9).
 */

#include "builder.h"
#include "builder_support.h"

#include "camel/core/mm.h"

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;

namespace camel::compile::gir {

Builder &Builder::of(const context_ptr_t &context) {
    ASSERT(context != nullptr, "The graph builder of a program needs its context.");
    auto &session = context->graphBuildSession();
    if (!session) {
        session = std::make_shared<Builder>(context);
    }
    return *std::static_pointer_cast<Builder>(session);
}

void Builder::claimRootNodes(size_t firstNode) {
    const size_t count = rootGraph_->draft().nodeSlotCount();
    if (rootNodeOwners_.size() < count) {
        rootNodeOwners_.resize(count, nullptr);
    }
    for (size_t id = firstNode; id < count; ++id) {
        if (rootNodeOwners_[id] == nullptr) {
            rootNodeOwners_[id] = unit_;
        }
    }
}

compile_graph_ptr_t
Builder::buildModule(const module_ptr_t &module, GCT::Tree &tree, diagnostics_ptr_t diags) {
    ensureRootGraph();
    auto &slot = units_[module.get()];
    if (!slot) {
        slot = std::make_unique<ModuleUnit>();
    }
    ModuleUnit &unit    = *slot;
    unit.module         = module;
    unit.tree           = &tree;
    unit.diags          = diags;
    unit.entry          = context_ && context_->mainModule() == module;
    unit.nodeScope      = node_scope_t::create();
    unit.graphScope     = graph_scope_t::create();
    unit.decoratedScope = decorated_scope_t::create();
    unit.templateScope  = template_scope_t::create();

    // The builder is in no other module's build when a module is built (a module's imports
    // are built while its construction tree is, before its graphs); still, everything that
    // names the module being built is put back on the way out.
    struct CurrentModule {
        Builder &self;
        ModuleUnit *unit;
        camel::core::module::module_ptr_t module;
        camel::core::error::diagnostics_ptr_t diags;
        GCT::Tree *tree;
        compile_graph_ptr_t graph;
        node_scope_ptr_t nodeScope;
        graph_scope_ptr_t graphScope;
        decorated_scope_ptr_t decoratedScope;
        template_scope_ptr_t templateScope;
        explicit CurrentModule(Builder &b)
            : self(b), unit(b.unit_), module(b.module_), diags(b.diags_), tree(b.tree_),
              graph(b.currGraph_), nodeScope(b.nodeScope_), graphScope(b.graphScope_),
              decoratedScope(b.decoratedScope_), templateScope(b.templateScope_) {}
        ~CurrentModule() {
            self.unit_           = unit;
            self.module_         = module;
            self.diags_          = diags;
            self.tree_           = tree;
            self.currGraph_      = graph;
            self.nodeScope_      = nodeScope;
            self.graphScope_     = graphScope;
            self.decoratedScope_ = decoratedScope;
            self.templateScope_  = templateScope;
        }
    } restore(*this);

    unit_                 = &unit;
    module_               = module;
    diags_                = diags;
    tree_                 = &tree;
    currGraph_            = rootGraph_;
    nodeScope_            = unit.nodeScope;
    graphScope_           = unit.graphScope;
    decoratedScope_       = unit.decoratedScope;
    templateScope_        = unit.templateScope;
    waited_               = false;
    synced_               = false;
    varied_               = false;
    declaresVariable_     = false;
    lastSyncedNode_       = {};
    typeBindings_.clear();
    instantiationDepth_   = 0;
    genericCalleeAllowed_ = false;
    recorder_             = context_ ? context_->semanticRecorder() : nullptr;

    const size_t firstRootNode = rootGraph_->draft().nodeSlotCount();
    const size_t firstSubGraph = rootGraph_->subGraphs().size();
    compile_graph_ptr_t result = rootGraph_;
    try {
        reportImportNameConflicts();
        visit(tree.root());
        claimRootNodes(firstRootNode);
        // The link step: every direct call gets the captures of its callee. Before the entry
        // call is made, so that `main` has all of its captures when its value is made.
        linkCaptureSites();
        if (unit.entry) {
            buildEntryCall();
            linkCaptureSites();
            claimRootNodes(firstRootNode);
        }
        reportIgnoredCapturedWrites();
        reportUndeclaredCapturedWrites();
        // Before the bodies are let go by reportDroppedEffectsOfBodies.
        reportUnusedFallibleValues();
        reportDroppedEffectsOfBodies();
        if (unit.entry) {
            // The top-level statements run only as far as the call of `main` depends on them.
            reportDroppedEffects(rootGraph_);
        }
    } catch (Diagnostic &d) {
        diags_->add(std::move(d));
        result = nullptr;
    } catch (const BuildAbortException &) {
        result = nullptr;
    } catch (const camel::core::mm::MemoryExhausted &e) {
        // A memory space is at its limit: reported like any other limit the program runs into,
        // at the function that was being compiled (buildFuncBody says which).
        camel::core::error::setPipelineStage("compile");
        auto diag = diags_->of(RuntimeDiag::MemoryExhausted);
        if (e.origin() != camel::source::kInvalidOriginId) {
            diag.atOrigin(e.origin());
        }
        diag.commit(std::string(), std::string(e.what()));
        camel::core::error::setPipelineStage("");
        result = nullptr;
    }
    claimRootNodes(firstRootNode);
    if (!unit.entry) {
        // The functions of an imported module are part of the program as far as something
        // calls them or makes their value, like any function the program does not use: they
        // are not written into the root graph's own functions, which every reachability walk
        // (encoding, the passes) starts from.
        const std::vector<compile_graph_ptr_t> added(
            rootGraph_->subGraphs().begin() + static_cast<std::ptrdiff_t>(firstSubGraph),
            rootGraph_->subGraphs().end());
        for (const compile_graph_ptr_t &graph : added) {
            rootGraph_->eraseSubGraph(graph);
        }
    }
    return result;
}

} // namespace camel::compile::gir
