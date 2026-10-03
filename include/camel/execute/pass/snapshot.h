/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF
 * ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 02, 2026
 * Updated: Oct. 02, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
