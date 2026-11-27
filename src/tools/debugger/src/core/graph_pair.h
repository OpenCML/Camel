// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/// The one place that pairs a view with the graph it is the view of.
///
/// Everything the debugger reads, it reads through the view (camel/graph/graph_view.h): a frame,
/// the index and the walks hold the view and nothing else. Two questions a reader of the running
/// program asks are not the view's -- where in the graph a node sits (the execution site a
/// diagnostic names) and the JSON dump of the graph it is stopped in -- and they are answered
/// where they are asked, by pairing the view with its graph here, rather than by the view growing
/// a second face or a frame keeping a second handle.
///
/// The pass pipeline's own boundary stops are not this case: the pipeline hands its hook the graph
/// a pass made, before anything reads it, and that hook is the representation's by definition.
///
/// `GraphHandle` is deliberately `Program`'s own answer and not a written type: naming the
/// representation type in this file would make this file one more place to change when the
/// representation changes, which is exactly what reading through the view avoids.

#include "camel/core/context/context.h"
#include "graph/graph_view.h"
#include "camel/runtime/program.h"

#include <type_traits>
#include <utility>

namespace debugger::core::pairing {

/// What `Program::graphs()` hands out: the graph itself. Not named here (see above).
using GraphHandle = std::remove_reference_t<
    decltype(std::declval<const camel::runtime::Program &>().graphs().front())>;

/// The graph `view` is the view of, among the graphs of the program `ctx` holds. A graph makes
/// its view once and keeps it, so the view's address names the graph. Null when the program does
/// not hold a graph whose view this is (a graph of an earlier run, or of another program).
inline GraphHandle
graphOf(const camel::core::context::context_ptr_t &ctx, const camel::graph::GraphView *view) {
    if (ctx == nullptr || view == nullptr) {
        return nullptr;
    }
    const camel::runtime::Program *program = ctx->runtimeProgram();
    if (program == nullptr) {
        return nullptr;
    }
    for (GraphHandle graph : program->graphs()) {
        if (graph != nullptr && &graph->view() == view) {
            return graph;
        }
    }
    return nullptr;
}

/// The view of a graph: the face a reader is given.
template <typename Handle> inline const camel::graph::GraphView *viewOf(Handle graph) {
    return graph == nullptr ? nullptr : &graph->view();
}

} // namespace debugger::core::pairing