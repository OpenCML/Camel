// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>
#include <limits>
#include <string>

namespace camel::core::type {
class TupleType;
} // namespace camel::core::type

namespace camel::runtime {
class GCGraph;
} // namespace camel::runtime

class Tuple;

namespace camel::runtime {

/// What a frame needs to know about the graph it runs: the tuple types of its two areas, whether
/// the frame layout is final, the frame's byte size, the static area, and the graph's name.
///
/// This record is a plain heap object, not a value of the collector's heap: it is made with the
/// graph and freed with it (`GCGraph`), and **a collection never moves it**. That is its reason to
/// exist. A frame keeps a pointer to it for its whole life, a life that spans collections, while
/// the graph object itself can be evacuated from one generation to the next; a pointer into the
/// graph would be stale after the first of those moves, a pointer to this record is not. The graph
/// rewrites the `graph` field in `GCGraph::onMoved()`, nothing else in it changes.
///
/// `camel/core/context/frame.h` is written against this record instead of against the graph, which
/// is what lets a frame's header not include `camel/runtime/graph.h`.
struct FrameLayout {
    /// The graph this record describes. Kept current by `GCGraph::onMoved()`.
    GCGraph *graph = nullptr;
    /// The tuple of the dynamic area (the frame's data layout), null until it is final.
    const camel::core::type::TupleType *runtimeDataType = nullptr;
    /// The tuple of the static area (the closure the frame was entered with), when it has one.
    const camel::core::type::TupleType *staticDataType = nullptr;
    /// The graph's own static area, the one a call uses when it brings none of its own.
    ::Tuple *staticArea = nullptr;
    /// Bytes from the frame's start to the frame after it (`GCGraph::frameSize()`).
    size_t frameSize = 0;
    /// The graph's name, copied here when the graph was made: log lines and fault messages read it
    /// from this record, so they do not need the graph's definition.
    std::string name;
    /// True once the frame layout is final (`GCGraph::hasFrameLayout()`).
    bool finalized = false;
};

} // namespace camel::runtime
