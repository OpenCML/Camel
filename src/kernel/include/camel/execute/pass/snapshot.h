// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstddef>

#include "camel/runtime/graph.h"

/**
 * Per-pass GIR snapshot hook — always compiled, even in NDEBUG, so tooling
 * (camel-db) can capture the graph after every pass on release builds. Cost
 * when no handler is installed is one atomic load per successfully applied
 * pass. The handler fires synchronously inside applyPassesDetailed right
 * after a pass returned a non-null graph; passes that consumed the graph or
 * failed do not fire. The graph pointer is only valid for the duration of
 * the call — serialize immediately, never retain it (the GC may move or
 * reclaim it).
 */
using PassSnapshotHandler = void (*)(
    const char *passName, size_t passOutputIndex, camel::runtime::GCGraph *graph, void *userData);

/// Install (or clear, with nullptr) the pass snapshot handler. Live in all build modes.
void setPassSnapshotHandler(PassSnapshotHandler handler, void *userData);
