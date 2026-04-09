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
 * Created: Apr. 08, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Runtime rewrite session boundary.
 *
 * Runtime passes should enter through GCGraph and treat runtime graph identity
 * as the primary entrypoint. The current implementation still delegates the
 * actual structural rewrite to compile-time GIR, but that delegation is kept
 * behind this session so later GCGraph-native mutation APIs can replace it
 * without changing every runtime pass entrypoint again.
 */

#pragma once

#include "camel/compile/gir/builder.h"
#include "camel/core/context/context.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/reachable.h"

#include <unordered_map>
#include <vector>

namespace camel::runtime {

class RuntimeSourceGraphEditSession;

class RuntimeGraphRewriteSession {
  public:
    RuntimeGraphRewriteSession(
        const camel::core::context::context_ptr_t &context, GCGraph *runtimeRoot)
        : context_(context), runtimeRoot_(runtimeRoot) {
        ASSERT(context_ != nullptr, "Runtime rewrite session requires a valid context.");
        ASSERT(runtimeRoot_ != nullptr, "Runtime rewrite session requires a runtime root graph.");
        sourceRoot_ = runtimeRoot_->compileGraphMetadata();
        ASSERT(sourceRoot_ != nullptr, "Runtime rewrite session requires source graph metadata.");
    }

    GCGraph *runtimeRoot() const { return runtimeRoot_; }
    const GIR::graph_ptr_t &sourceRoot() const { return sourceRoot_; }

    std::vector<GCGraph *> collectReachableRuntimeGraphs() const {
        return camel::runtime::collectReachableGraphs(runtimeRoot_);
    }

    RuntimeSourceGraphEditSession createSourceEditSession() const;

    GCGraph *rematerialize(const GIR::graph_ptr_t &rewrittenRoot) {
        if (!rewrittenRoot) {
            runtimeRoot_ = nullptr;
            sourceRoot_.reset();
            return nullptr;
        }
        // Keep runtime graph materialization centralized in Context so rewrite
        // passes do not own runtime graph lifetimes directly.
        runtimeRoot_ = context_->materializeRuntimeRoot(rewrittenRoot);
        ASSERT(runtimeRoot_ != nullptr, "Runtime rewrite rematerialization returned null root.");
        sourceRoot_ = runtimeRoot_->compileGraphMetadata();
        ASSERT(sourceRoot_ != nullptr, "Rematerialized runtime root is missing compile metadata.");
        return runtimeRoot_;
    }

    GIR::graph_ptr_t finish(const GIR::graph_ptr_t &rewrittenRoot) {
        if (!rewrittenRoot) {
            return rewrittenRoot;
        }
        (void)rematerialize(rewrittenRoot);
        return rewrittenRoot;
    }

  private:
    std::vector<GIR::graph_ptr_t> collectReachableSourceGraphs() const;

    camel::core::context::context_ptr_t context_;
    GCGraph *runtimeRoot_ = nullptr;
    GIR::graph_ptr_t sourceRoot_;
};

struct RuntimeStaticValueMaterializationOptions {
    bool propagateMacro = false;
};

/*
 * RuntimeSourceGraphEditSession is the minimal runtime-side source-graph editor
 * used by runtime passes that still need GIR node payloads today. It clones
 * the exact runtime-reachable source closure, exposes source->clone mappings,
 * supports static-value materialization into the cloned closure, and seals the
 * rewritten clone tree on finish. This keeps runtime passes on a dedicated
 * runtime-side editing path.
 */
class RuntimeSourceGraphEditSession {
  public:
    RuntimeSourceGraphEditSession(
        const GIR::graph_ptr_t &sourceRoot,
        const std::vector<GIR::graph_ptr_t> &reachableSourceGraphs);

    const GIR::graph_ptr_t &sourceRoot() const { return sourceRoot_; }
    const GIR::graph_ptr_t &root() const { return root_; }
    const std::vector<GIR::graph_ptr_t> &reachableSourceGraphs() const {
        return reachableSourceGraphs_;
    }
    std::vector<GIR::graph_ptr_t> editableGraphs() const;

    GIR::graph_ptr_t canonicalGraph(const GIR::graph_ptr_t &graph) const;
    GIR::Node *canonicalNode(const GIR::Node *node) const;
    bool ownsGraph(const GIR::Graph *graph) const;
    bool hasDraftGraph(const GIR::Graph *graph) const { return ownsGraph(graph); }

    GIR::Node *materializeStaticValue(
        const GIR::graph_ptr_t &owner, slot_t slot, GIR::Type *type,
        const RuntimeStaticValueMaterializationOptions &options = {});

    bool replaceNode(GIR::Node *oldNode, GIR::Node *newNode);
    bool replaceAllUses(GIR::Node *oldNode, GIR::Node *newNode);
    void eraseNode(GIR::Node *node);
    void link(GIR::LinkType type, GIR::Node *from, GIR::Node *to);
    void addDependency(const GIR::graph_ptr_t &owner, const GIR::graph_ptr_t &dependency);
    GIR::graph_ptr_t importDependency(
        const GIR::graph_ptr_t &owner, const GIR::graph_ptr_t &dependency,
        camel::compile::gir::GraphImportMode mode =
            camel::compile::gir::GraphImportMode::ReferenceOnly);
    void eraseDependency(const GIR::graph_ptr_t &owner, const GIR::graph_ptr_t &dependency);
    void eraseSubGraph(const GIR::graph_ptr_t &owner, const GIR::graph_ptr_t &subGraph);
    camel::compile::gir::InlineResult
    inlineCallable(GIR::Node *node, const camel::compile::gir::InlineOptions &options = {});
    void adoptOwnedGraph(const GIR::graph_ptr_t &graph);
    void markChanged() { changed_ = true; }

    GIR::graph_ptr_t finish();

  private:
    void cloneReachableClosure(const std::vector<GIR::graph_ptr_t> &reachableSourceGraphs);
    GIR::graph_ptr_t cloneSourceGraph(const GIR::graph_ptr_t &sourceGraph);
    void retargetClonedClosure();
    void retargetGraphRegistries(const GIR::graph_ptr_t &graph);
    void retargetNodeGraphRefs(const GIR::graph_ptr_t &graph);
    void normalizeOwnedStaticValues(const GIR::graph_ptr_t &graph);

    GIR::graph_ptr_t sourceRoot_;
    GIR::graph_ptr_t root_;
    std::vector<GIR::graph_ptr_t> reachableSourceGraphs_;
    bool changed_ = false;
    std::unordered_set<GIR::Graph *> ownedGraphs_;
    std::unordered_map<const GIR::Graph *, GIR::graph_ptr_t> clonedBySource_;
    std::unordered_map<const GIR::Node *, GIR::Node *> clonedNodeBySource_;
};

} // namespace camel::runtime
