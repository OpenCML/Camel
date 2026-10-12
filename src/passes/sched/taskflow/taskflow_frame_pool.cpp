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

namespace {

// The pools that exist, by id. A thread's cache can outlive a pool (the main thread runs one
// pass after another); frames of a pool that is gone are dropped, not given back. Never
// destroyed: thread caches are flushed when their thread ends, which can be after the statics.
struct LivePools {
    std::mutex mutex;
    std::unordered_set<uint64_t> ids;
};
LivePools &livePools() {
    static LivePools *pools = new LivePools();
    return *pools;
}

// How many frames a thread takes from the shared list of a graph at once, and how many it
// keeps before it gives half of them back.
constexpr size_t kLocalRefill = 16;
constexpr size_t kLocalMax    = 64;

} // namespace

// The frames of one graph that the calling thread can take and give back without a lock.
// A call takes a frame and gives it back on the same thread (the callee runs on the caller's
// native stack), so in steady state a thread only touches its own list. With one list per
// graph behind a mutex, the workers that ran the same function (every element of a `map`)
// queued for that mutex twice per call.
struct TaskflowFramePool::LocalArena {
    uint64_t poolId   = 0;
    uintptr_t key     = 0;
    GraphArena *arena = nullptr;
    std::vector<Frame *> free;
};

struct TaskflowFramePool::ThreadCache {
    std::array<LocalArena, 128> entries{};
    ~ThreadCache() {
        for (LocalArena &local : entries) {
            giveBack(local);
        }
    }
};

TaskflowFramePool::ThreadCache &TaskflowFramePool::threadCache() {
    thread_local ThreadCache cache;
    return cache;
}

TaskflowFramePool::TaskflowFramePool(size_t chunkBytes, size_t minChunkFrames)
    : chunkBytes_(chunkBytes), minChunkFrames_(std::max<size_t>(1, minChunkFrames)),
      poolId_(nextPoolId()) {
    {
        std::scoped_lock lock(livePools().mutex);
        livePools().ids.insert(poolId_);
    }
    camel::core::mm::autoSpace().registerExternalRootTracer(
        this,
        "TaskflowFramePool.activeFrames",
        [this](const camel::core::mm::GenerationalAllocatorWithGC::RefRelocator &relocate) {
            traceActiveFrames(relocate);
        });
}

TaskflowFramePool::~TaskflowFramePool() {
    camel::core::mm::autoSpace().unregisterExternalRootTracer(this);
    {
        // From here on no thread gives a cached frame back to this pool.
        std::scoped_lock lock(livePools().mutex);
        livePools().ids.erase(poolId_);
    }
    for (auto &[_, arena] : arenas_) {
        for (const GraphArena::Chunk &chunk : arena->chunks)
            std::free(chunk.base);
    }
}

uint64_t TaskflowFramePool::nextPoolId() {
    static std::atomic<uint64_t> next{1};
    return next.fetch_add(1, std::memory_order_relaxed);
}

uintptr_t TaskflowFramePool::arenaKey(camel::runtime::GCGraph *graph) {
    return reinterpret_cast<uintptr_t>(graph);
}

// A frame is active between acquire and release. The mark is the frame's own `next_` field
// (the link of the linear FramePool, which this pool has no use for): the frame itself when
// active, null when free. The collector reads it for every frame of every chunk
// (traceActiveFrames). A set of the active frames of each graph cost a hash insert and an
// erase per call, under the graph's mutex.
bool TaskflowFramePool::isActive(Frame *frame) {
    return std::atomic_ref<Frame *>(frame->next_).load(std::memory_order_relaxed) != nullptr;
}

void TaskflowFramePool::setActive(Frame *frame, bool active) {
    std::atomic_ref<Frame *>(frame->next_)
        .store(active ? frame : nullptr, std::memory_order_relaxed);
}

void TaskflowFramePool::giveBack(LocalArena &local) {
    if (local.arena == nullptr || local.free.empty()) {
        return;
    }
    // The pool may be gone (its memory with it): then there is nothing to give back to.
    std::scoped_lock live(livePools().mutex);
    if (livePools().ids.contains(local.poolId)) {
        std::scoped_lock lock(local.arena->mutex);
        local.arena->freeFrames.insert(
            local.arena->freeFrames.end(),
            local.free.begin(),
            local.free.end());
    }
    local.free.clear();
}

TaskflowFramePool::LocalArena &TaskflowFramePool::localArena(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph is null.");
    // An arena lives as long as its pool; the pool is told apart by its id, not by its
    // address. Only the first use of a graph on a thread (or the first after another graph
    // took its place in the thread's table) takes locks.
    const uintptr_t key = arenaKey(graph);
    auto &entries       = threadCache().entries;
    LocalArena &local   = entries[(key >> 4 ^ key >> 11 ^ key >> 18) % entries.size()];
    if (local.poolId == poolId_ && local.key == key) [[likely]] {
        return local;
    }
    if (local.poolId == poolId_ && local.arena != nullptr) {
        // Of this pool, which is alive: no need to ask.
        std::scoped_lock lock(local.arena->mutex);
        local.arena->freeFrames.insert(
            local.arena->freeFrames.end(),
            local.free.begin(),
            local.free.end());
        local.free.clear();
    } else {
        giveBack(local);
    }
    local.poolId = poolId_;
    local.key    = key;
    local.arena  = &getOrCreateArena(graph);
    return local;
}

TaskflowFramePool::GraphArena &TaskflowFramePool::getOrCreateArena(camel::runtime::GCGraph *graph) {
    ASSERT(graph != nullptr, "Runtime graph is null.");
    const uintptr_t key = arenaKey(graph);
    std::scoped_lock lock(arenasMutex_);
    auto it = arenas_.find(key);
    if (it == arenas_.end()) {
        auto arena          = std::make_unique<GraphArena>();
        arena->runtimeGraph = graph;
        it                  = arenas_.emplace(key, std::move(arena)).first;
    }
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
    // Listed last: the collector walks the frames of every chunk listed.
    arena.chunks.push_back({chunk, frameCount});
}

Frame *TaskflowFramePool::acquire(camel::runtime::GCGraph *graph) {
    LocalArena &local = localArena(graph);
    if (local.free.empty()) {
        GraphArena &arena = *local.arena;
        std::scoped_lock lock(arena.mutex);
        if (arena.freeFrames.empty())
            allocateChunk(arena, 1);
        const size_t take = std::min(arena.freeFrames.size(), kLocalRefill);
        local.free.assign(
            arena.freeFrames.end() - static_cast<std::ptrdiff_t>(take),
            arena.freeFrames.end());
        arena.freeFrames.resize(arena.freeFrames.size() - take);
    }
    Frame *frame = local.free.back();
    local.free.pop_back();
#ifndef NDEBUG
    std::fill_n(
        frame->slotBase(),
        local.arena->runtimeDataType->size(),
        camel::core::mm::kDebugUninitializedSlot);
#endif
    clearGcSlots(frame);
    setActive(frame, true);
    return frame;
}

void TaskflowFramePool::release(Frame *frame) {
    ASSERT(frame != nullptr, "Frame is null.");
    ASSERT(
        frame->runtimeGraph() != nullptr,
        "Taskflow runtime frame pool can only release frames bound to a runtime graph.");
    LocalArena &local = localArena(frame->runtimeGraph());
    setActive(frame, false);
    local.free.push_back(frame);
    if (local.free.size() > kLocalMax) [[unlikely]] {
        // A deep recursion unwinding, or frames released on a thread that did not take them.
        std::scoped_lock lock(local.arena->mutex);
        local.arena->freeFrames.insert(
            local.arena->freeFrames.end(),
            local.free.begin() + static_cast<std::ptrdiff_t>(kLocalMax / 2),
            local.free.end());
        local.free.resize(kLocalMax / 2);
    }
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
        for (const GraphArena::Chunk &chunk : arena->chunks) {
            for (size_t i = 0; i < chunk.frames; ++i) {
                Frame *frame = reinterpret_cast<Frame *>(chunk.base + i * arena->frameSize);
                if (!isActive(frame)) {
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
}

} // namespace camel::core::context
