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
 * Created: Oct. 21, 2024
 * Updated: Oct. 02, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/execute/pass/runtime_trans.h"

#include <unordered_set>

struct GraphVizDumpConfig {
    bool readableOnly = false;
    /// Marks nodes with their boundary roles (camel/runtime/node_roles.h): a `role` attribute,
    /// and in the styled dump a fill color per role and a legend.
    bool annotateRoles = false;
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

    /// One-shot readable dump (rgir-style DOT: label/type only, plus origin/span/srcfile
    /// attributes) of an arbitrary runtime graph. Single-call use by tooling such as
    /// camel-db; pass instances are normally used via apply() instead.
    std::string dumpReadable(camel::runtime::GCGraph *graph);

    camel::runtime::GCGraph *apply(camel::runtime::GCGraph *graph, std::ostream &os) override;
};

/// Reusable entry point for tooling (e.g. camel-db): dumps a runtime graph as rgir-style
/// readable DOT via a temporary readable-only GraphVizDumpPass bound to ctx. Returns an
/// empty string when the graph is null or has no materialized node payload. The graph must
/// stay rooted by the caller for the duration of the call.
std::string dumpReadableRuntimeGraph(
    camel::runtime::GCGraph *graph, const camel::core::context::context_ptr_t &ctx);
