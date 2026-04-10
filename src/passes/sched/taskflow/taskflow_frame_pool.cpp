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
 * Created: Mar. 09, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "taskflow.h"

#include <algorithm>
#include <cstdlib>

using namespace GIR;

namespace camel::core::context {

TaskflowFramePool::TaskflowFramePool(size_t chunkBytes, size_t minChunkFrames)
    : chunkBytes_(chunkBytes), minChunkFrames_(std::max<size_t>(1, minChunkFrames)) {}

TaskflowFramePool::~TaskflowFramePool() {
    for (auto &[_, arena] : arenas_) {
        for (std::byte *chunk : arena->chunks)
            std::free(chunk);
    }
}

uintptr_t TaskflowFramePool::arenaKey(camel::runtime::GCGraph *graph) {
    return reinterpret_cast<uintptr_t>(graph);
}

TaskflowFramePool::GraphArena &TaskflowFramePool::getOrCreateArena(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph is null.");
    std::scoped_lock lock(arenasMutex_);
    auto it = arenas_.find(arenaKey(graph));
    if (it != arenas_.end()) {
        return *it->second;
    }

    auto arena               = std::make_unique<GraphArena>();
    arena->runtimeGraph      = graph;
    auto &ref                = *arena;
    arenas_[arenaKey(graph)] = std::move(arena);
    return ref;
}

void TaskflowFramePool::allocateChunk(GraphArena &arena, size_t minFrameCount) {
    if (arena.runtimeDataType == nullptr || arena.staticArea == nullptr) {
        ASSERT(arena.runtimeGraph != nullptr, "Taskflow runtime arena has null graph.");
        ASSERT(
            arena.runtimeGraph->hasFrameLayout(),
            std::format(
                "Runtime graph '{}' has no finalized frame layout.",
                arena.runtimeGraph->name()));
        arena.frameSize       = arena.runtimeGraph->frameSize();
        arena.runtimeDataType = arena.runtimeGraph->runtimeDataType();
        arena.staticArea      = arena.runtimeGraph->staticArea();
        ASSERT(arena.frameSize > 0, "Frame size must be positive.");
        arena.chunkFrames = std::max(minChunkFrames_, chunkBytes_ / arena.frameSize);
        if (arena.chunkFrames == 0)
            arena.chunkFrames = 1;
    }

    size_t frameCount = std::max(arena.chunkFrames, minFrameCount);
    size_t chunkSize  = arena.frameSize * frameCount;
    std::byte *chunk  = reinterpret_cast<std::byte *>(std::malloc(chunkSize));
    if (chunk == nullptr)
        throw std::bad_alloc{};

    arena.chunks.push_back(chunk);
    arena.freeFrames.reserve(arena.freeFrames.size() + frameCount);
    for (size_t i = 0; i < frameCount; ++i) {
        std::byte *slot = chunk + i * arena.frameSize;
        auto *frame = new (slot) Frame(arena.runtimeGraph, arena.staticArea, arena.runtimeDataType);
#ifndef NDEBUG
        std::fill_n(
            frame->slotBase(),
            arena.runtimeDataType->size(),
            camel::core::mm::kDebugUninitializedSlot);
#endif
        arena.freeFrames.push_back(frame);
    }
}

Frame *TaskflowFramePool::acquire(camel::runtime::GCGraph *graph) {
    GraphArena &arena = getOrCreateArena(graph);
    std::scoped_lock lock(arena.mutex);
    if (arena.freeFrames.empty())
        allocateChunk(arena, 1);
    Frame *frame = arena.freeFrames.back();
    arena.freeFrames.pop_back();
#ifndef NDEBUG
    std::fill_n(
        frame->slotBase(),
        arena.runtimeDataType->size(),
        camel::core::mm::kDebugUninitializedSlot);
#endif
    return frame;
}

void TaskflowFramePool::release(Frame *frame) {
    ASSERT(frame != nullptr, "Frame is null.");
    ASSERT(
        frame->runtimeGraph() != nullptr,
        "Taskflow runtime frame pool can only release frames bound to a runtime graph.");
    GraphArena &arena = getOrCreateArena(frame->runtimeGraph());
    std::scoped_lock lock(arena.mutex);
    arena.freeFrames.push_back(frame);
}

void TaskflowFramePool::warmup(camel::runtime::GCGraph *graph, size_t count) {
    if (count == 0)
        return;
    GraphArena &arena = getOrCreateArena(graph);
    std::scoped_lock lock(arena.mutex);
    if (arena.freeFrames.size() >= count)
        return;
    allocateChunk(arena, count - arena.freeFrames.size());
}

} // namespace camel::core::context
