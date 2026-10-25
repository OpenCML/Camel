// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/execute/pass/runtime_trans.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

struct GraphVizDumpConfig {
    bool rich = false;
    /// Marks nodes with their boundary roles (camel/runtime/node_roles.h): a `role` attribute,
    /// and in the styled dump a fill color per role and a legend.
    bool annotateRoles = false;
    /// Draws the regions of a graph (camel/runtime/graph.h, GCRegion) as clusters inside the
    /// cluster of the function; off by default, so the dumps of graphs with regions look as
    /// they did.
    bool regions = false;
};

class GraphVizDumpPass : public RuntimeGraphTranslatePass {
    bool showRawPtr = false;
    GraphVizDumpConfig config_;
    std::unordered_map<std::string, size_t> ptrCnt_;
    std::unordered_map<std::string, std::unordered_map<uintptr_t, size_t>> ptrsMap_;
    std::unordered_set<camel::runtime::GCGraph *> visitedGraphs_;
    // Roles seen so far, in first-seen order, for the legend.
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> rolesSeen_;

    size_t depth_ = 0;
    std::string baseIndent_;
    const camel::runtime::Program *program_ = nullptr;
    const std::string indent_ = "    ";

    void pushIndent();
    void popIndent();

    std::string pointerToIdent(const void *ptr, const char *prefix = "N");

    /// Debugger-facing origin attributes for the readable dump: `origin=<id>`, and when the
    /// span resolves, `span="sl:sc-el:ec"` (0-based, LSP-style) plus `srcfile="..."`. Empty
    /// when the node has no recorded origin. Lets the VSCode GIR panel match DOT nodes against
    /// gir-json nodes and jump to source.
    std::string debugOriginAttr(camel::runtime::GCGraph *graph, camel::runtime::gc_node_ref_t ref);

    std::string dumpGraph(camel::runtime::GCGraph *graph);

  public:
    GraphVizDumpPass(const camel::core::context::context_ptr_t &context);
    GraphVizDumpPass(const camel::core::context::context_ptr_t &context, GraphVizDumpConfig config);
    virtual ~GraphVizDumpPass() = default;

    /// One-shot compact dump (gir-style DOT: label/type and semantic metadata, including
    /// attributes) of an arbitrary runtime graph. Single-call use by tooling such as
    /// camel-db; pass instances are normally used via apply() instead.
    std::string dumpDot(camel::runtime::GCGraph *graph);

    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;

    /// A graph with values that have several writings is drawn as it is: every class is a dashed
    /// cluster of its writings, and the writing extraction would choose is marked, rather than
    /// the driver picking one writing per value first.
    bool acceptsWritings() const override { return true; }
};

/// Reusable entry point for tooling (e.g. camel-db): dumps a runtime graph as gir-style
/// readable DOT via a temporary readable-only GraphVizDumpPass bound to ctx. Returns an
/// empty string when the graph is null or has no materialized node payload. The graph must
/// stay rooted by the caller for the duration of the call.
std::string dumpCompactRuntimeGraph(
    camel::runtime::GCGraph *graph, const camel::core::context::context_ptr_t &ctx);
