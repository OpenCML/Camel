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
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "taskflow.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>

namespace camel::core::context {

TaskflowFramePool::TaskflowFramePool(size_t chunkBytes, size_t minChunkFrames)
    : chunkBytes_(chunkBytes), minChunkFrames_(std::max<size_t>(1, minChunkFrames)),
      poolId_(nextPoolId()) {
    camel::core::mm::autoSpace().registerExternalRootTracer(
        this,
        "TaskflowFramePool.activeFrames",
        [this](const camel::core::mm::GenerationalAllocatorWithGC::RefRelocator &relocate) {
            traceActiveFrames(relocate);
        });
}

TaskflowFramePool::~TaskflowFramePool() {
    camel::core::mm::autoSpace().unregisterExternalRootTracer(this);
    for (auto &[_, arena] : arenas_) {
        for (std::byte *chunk : arena->chunks)
            std::free(chunk);
    }
}

uint64_t TaskflowFramePool::nextPoolId() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

uintptr_t TaskflowFramePool::arenaKey(camel::runtime::GCGraph *graph) {
    return reinterpret_cast<uintptr_t>(graph);
}

TaskflowFramePool::GraphArena &TaskflowFramePool::getOrCreateArena(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph is null.");
    // Every call takes a frame and gives it back, on every worker. The arenas a thread used
    // last are remembered per thread, so that only the first use of a graph on a thread takes
    // the lock of the table, which all workers share. An arena lives as long as its pool; the
    // pool is told apart by its id, not by its address.
    struct CachedArena {
        uint64_t poolId   = 0;
        uintptr_t key     = 0;
        GraphArena *arena = nullptr;
    };
    thread_local std::array<CachedArena, 16> cache{};
    const uintptr_t key = arenaKey(graph);
    CachedArena &cached = cache[(key >> 4 ^ key >> 12) % cache.size()];
    if (cached.poolId == poolId_ && cached.key == key) {
        return *cached.arena;
    }

    std::scoped_lock lock(arenasMutex_);
    auto it = arenas_.find(key);
    if (it == arenas_.end()) {
        auto arena          = std::make_unique<GraphArena>();
        arena->runtimeGraph = graph;
        it                  = arenas_.emplace(key, std::move(arena)).first;
    }
    cached = {poolId_, key, it->second.get()};
    return *it->second;
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
        arena.nextChunkFrames = std::min(minChunkFrames_, arena.chunkFrames);
    }

    // A graph starts with a few frames and each further chunk doubles, up to a full chunk.
    // Every graph the scheduler prepares gets a chunk (warmup), and most are never active in
    // more than a handful of frames at once: a full chunk for each (1 MB, constructed frame by
    // frame) made a program of 2000 functions take 2.4 GB and most of its start-up time.
    size_t frameCount     = std::max(arena.nextChunkFrames, minFrameCount);
    arena.nextChunkFrames = std::min(arena.chunkFrames, arena.nextChunkFrames * 2);
    size_t chunkSize      = arena.frameSize * frameCount;
    std::byte *chunk      = reinterpret_cast<std::byte *>(std::malloc(chunkSize));
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
        clearGcSlots(frame);
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
    clearGcSlots(frame);
    arena.activeFrames.insert(frame);
    return frame;
}

void TaskflowFramePool::release(Frame *frame) {
    ASSERT(frame != nullptr, "Frame is null.");
    ASSERT(
        frame->runtimeGraph() != nullptr,
        "Taskflow runtime frame pool can only release frames bound to a runtime graph.");
    GraphArena &arena = getOrCreateArena(frame->runtimeGraph());
    std::scoped_lock lock(arena.mutex);
    arena.activeFrames.erase(frame);
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

void TaskflowFramePool::clearGcSlots(Frame *frame) const {
    ASSERT(frame != nullptr, "Cannot clear a null taskflow frame.");
    const auto *layout = frame->dynamicAreaType_;
    if (!layout || layout->refCount() == 0) {
        return;
    }
    const size_t *refs = layout->refs();
    for (size_t i = 0; i < layout->refCount(); ++i) {
        frame->dynamicArea_[refs[i]] = NullSlot;
    }
}

void TaskflowFramePool::traceActiveFrames(
    const camel::core::mm::GenerationalAllocatorWithGC::RefRelocator &relocate) {
    std::scoped_lock arenasLock(arenasMutex_);
    for (auto &[_, arena] : arenas_) {
        std::scoped_lock arenaLock(arena->mutex);
        for (Frame *frame : arena->activeFrames) {
            if (!frame) {
                continue;
            }
            if (frame->staticArea_ && frame->staticDataLayout()) {
                frame->staticArea_->updateRefs(relocate, frame->staticDataLayout());
            }
            if (frame->dynamicAreaType_) {
                frame->updateRefs(relocate, nullptr);
            }
        }
    }
}

} // namespace camel::core::context
