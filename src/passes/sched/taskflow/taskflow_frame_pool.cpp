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
 * Updated: Mar. 09, 2026
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

TaskflowFramePool::GraphArena &TaskflowFramePool::getOrCreateArena(Graph *graph) {
    std::scoped_lock lock(arenasMutex_);
    auto it = arenas_.find(graph);
    if (it != arenas_.end())
        return *it->second;

    auto arena     = std::make_unique<GraphArena>();
    arena->graph   = graph;
    auto &ref      = *arena;
    arenas_[graph] = std::move(arena);
    return ref;
}

void TaskflowFramePool::allocateChunk(GraphArena &arena, size_t minFrameCount) {
    if (arena.meta == nullptr) {
        arena.meta = arena.graph->getExtra<FrameMeta, 0>();
        if (arena.meta == nullptr)
            arena.meta = installFrameMetaInfoForGraph(arena.graph);
        arena.frameSize = arena.meta->frameSize;
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
        auto *frame =
            new (slot) Frame(arena.graph, arena.meta->staticArea, arena.meta->runtimeDataType);
        arena.freeFrames.push_back(frame);
    }
}

Frame *TaskflowFramePool::acquire(Graph *graph) {
    GraphArena &arena = getOrCreateArena(graph);
    std::scoped_lock lock(arena.mutex);
    if (arena.freeFrames.empty())
        allocateChunk(arena, 1);
    Frame *frame = arena.freeFrames.back();
    arena.freeFrames.pop_back();
    return frame;
}

void TaskflowFramePool::release(Frame *frame) {
    ASSERT(frame != nullptr, "Frame is null.");
    GraphArena &arena = getOrCreateArena(frame->graph());
    std::scoped_lock lock(arena.mutex);
    arena.freeFrames.push_back(frame);
}

void TaskflowFramePool::warmup(Graph *graph, size_t count) {
    if (count == 0)
        return;
    GraphArena &arena = getOrCreateArena(graph);
    std::scoped_lock lock(arena.mutex);
    if (arena.freeFrames.size() >= count)
        return;
    allocateChunk(arena, count - arena.freeFrames.size());
}

} // namespace camel::core::context
