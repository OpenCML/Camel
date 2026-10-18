/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 18, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Bounded, backend-private reuse of raw device and pinned-host blocks.
#include "tiles/internal/cuda_backend.h"
#ifdef CAMEL_TILES_CUDA
#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace camel::tiles::cuda_detail {
namespace {
// One size class for both kinds. It also keeps every driver request at least one
// byte, including a zero-byte logical value.
constexpr size_t kCapacityGranularity = 256;
} // namespace

MemoryPolicy MemoryPolicy::uncached() {
    MemoryPolicy policy;
    policy.device = PoolPolicy{};
    policy.host   = PoolPolicy{};
    return policy;
}
MemoryPool::MemoryPool(Api &api, PoolKind kind, PoolPolicy policy) noexcept
    : api_(api), kind_(kind), policy_(policy) {
    // Only kMaxIdleEntries slots exist; a larger request cannot be honored.
    policy_.maxEntries = std::min(policy_.maxEntries, kMaxIdleEntries);
}
MemoryPool::~MemoryPool() noexcept {
    // The backend trims explicitly while its context is still valid. This is a
    // defensive second pass so a standalone pool never leaves blocks cached.
    drainIdle();
}
void MemoryPool::bindContext(CUcontext context) noexcept { context_ = context; }
size_t MemoryPool::capacityFor(size_t bytes) {
    const size_t request = bytes ? bytes : 1;
    if (request > std::numeric_limits<size_t>::max() - (kCapacityGranularity - 1))
        throw std::overflow_error("Tiles allocation size overflows the rounded capacity");
    return (request + kCapacityGranularity - 1) / kCapacityGranularity * kCapacityGranularity;
}
MemoryPool::Block MemoryPool::takeIdleLocked(size_t wanted) noexcept {
    if (policy_.maxEntries == 0 || policy_.idleBytes == 0 || wanted > policy_.idleBytes)
        return {};
    size_t best = kMaxIdleEntries;
    for (size_t i = 0; i < idleCount_; ++i) {
        if (idle_[i].capacity < wanted)
            continue;
        if (best == kMaxIdleEntries || idle_[i].capacity < idle_[best].capacity)
            best = i;
    }
    if (best == kMaxIdleEntries)
        return {};
    // Best fit among size classes: the smallest cached block that still holds
    // the request. A small request takes a larger block only when no smaller
    // cached block is available.
    const Block block = idle_[best];
    idle_[best]       = idle_[--idleCount_];
    idleBytes_ -= block.capacity;
    ++reuses_;
    return block;
}
CUresult MemoryPool::allocateRaw(void **handle, size_t bytes) noexcept {
    if (kind_ == PoolKind::Device) {
        CUdeviceptr pointer = 0;
        const auto result   = api_.cuMemAlloc(&pointer, bytes);
        if (result == CUDA_SUCCESS)
            *handle = reinterpret_cast<void *>(pointer);
        return result;
    }
    return api_.cuMemHostAlloc(handle, bytes, 0);
}
MemoryPool::Block MemoryPool::allocate(size_t wanted) {
    // Both kinds need the context active: cuMemHostAlloc maps the block into the
    // owning context and cuMemFreeHost later unmaps it there.
    Current current(api_, context_);
    void *handle    = nullptr;
    CUresult result = allocateRaw(&handle, wanted);
    if (result == CUDA_ERROR_OUT_OF_MEMORY) {
        // Reclaim this kind's idle blocks outside the bookkeeping lock. The
        // retry is exactly one attempt; any other driver error is final.
        ++oomRetries_;
        drainIdle();
        result = allocateRaw(&handle, wanted);
    }
    api_.check(
        result,
        kind_ == PoolKind::Device ? "allocate device memory" : "allocate pinned host memory");
    ++allocations_;
    return Block{handle, wanted};
}
MemoryPool::Block MemoryPool::checkout(size_t bytes) {
    const size_t wanted = capacityFor(bytes);
    Block cached;
    {
        std::lock_guard lock(mutex_);
        if (disabled_)
            throw std::runtime_error("CUDA memory reuse is disabled after a drain failure");
        cached = takeIdleLocked(wanted);
    }
    ++checkouts_;
    if (cached.handle)
        return cached;
    return allocate(wanted);
}
bool MemoryPool::absorbLocked(Block block) noexcept {
    if (disabled_ || policy_.maxEntries == 0 || policy_.idleBytes == 0 ||
        block.capacity > policy_.idleBytes || idleCount_ >= policy_.maxEntries ||
        idleBytes_ > policy_.idleBytes - block.capacity)
        return false;
    idle_[idleCount_++] = block;
    idleBytes_ += block.capacity;
    ++cached_;
    return true;
}
void MemoryPool::freeBlock(Block block) noexcept {
    if (!block.handle)
        return;
    if (!context_ || api_.cuCtxPushCurrent(context_) != CUDA_SUCCESS) {
        // Without the owning context the block cannot be released safely.
        ++abandoned_;
        return;
    }
    const CUresult result = kind_ == PoolKind::Device
                                ? api_.cuMemFree(reinterpret_cast<CUdeviceptr>(block.handle))
                                : api_.cuMemFreeHost(block.handle);
    CUcontext popped      = nullptr;
    api_.cuCtxPopCurrent(&popped);
    // The counters must never claim a release that the driver refused.
    if (result == CUDA_SUCCESS)
        ++freed_;
    else
        ++abandoned_;
}
void MemoryPool::release(Block block) noexcept {
    if (!block.handle)
        return;
    {
        std::lock_guard lock(mutex_);
        if (absorbLocked(block))
            return;
        if (disabled_) {
            // A block that may still be the target of pending device work is
            // deliberately neither cached nor freed; dropping it is the safe
            // outcome of a drain or context failure.
            ++abandoned_;
            return;
        }
    }
    // Capacity or slot overflow drops the block; the driver call is outside the
    // bookkeeping lock and neither allocates nor throws.
    freeBlock(block);
}
size_t MemoryPool::drainIdle() noexcept {
    // Fixed-capacity staging: return bookkeeping never allocates.
    std::array<Block, kMaxIdleEntries> blocks{};
    size_t count = 0;
    {
        std::lock_guard lock(mutex_);
        count = idleCount_;
        for (size_t i = 0; i < count; ++i)
            blocks[i] = idle_[i];
        idleCount_ = 0;
        idleBytes_ = 0;
    }
    for (size_t i = 0; i < count; ++i)
        freeBlock(blocks[i]);
    ++trims_;
    return count;
}
void MemoryPool::trim() noexcept { drainIdle(); }
void MemoryPool::disableReuse() noexcept {
    // Set under the bookkeeping lock so a concurrent release either caches a
    // block before this point or observes the disabled state and abandons it.
    // Idle blocks are already drained and are left for the backend's final trim.
    std::lock_guard lock(mutex_);
    disabled_ = true;
}
PoolStats MemoryPool::stats() const {
    PoolStats result;
    {
        std::lock_guard lock(mutex_);
        result.idleEntries = idleCount_;
        result.idleBytes   = idleBytes_;
        result.disabled    = disabled_;
    }
    result.checkouts   = checkouts_;
    result.reuses      = reuses_;
    result.allocations = allocations_;
    result.cached      = cached_;
    result.freed       = freed_;
    result.trims       = trims_;
    result.oomRetries  = oomRetries_;
    result.abandoned   = abandoned_;
    return result;
}
bool MemoryPool::holdsIdle(uintptr_t handle) const noexcept {
    std::lock_guard lock(mutex_);
    return std::any_of(idle_.begin(), idle_.begin() + idleCount_, [handle](const Block &block) {
        return reinterpret_cast<uintptr_t>(block.handle) == handle;
    });
}

AllocationLease::AllocationLease(
    std::shared_ptr<Backend> backend, PoolKind kind, MemoryPool::Block block) noexcept
    : backend_(std::move(backend)), kind_(kind), block_(block) {}
AllocationLease::~AllocationLease() noexcept {
    // Last-share release. The pool reference stays valid because this lease
    // still owns a backend reference, and the block must return to the pool
    // before that reference is dropped.
    if (backend_ && block_.handle)
        backend_->pool(kind_).release(block_);
    block_ = {};
    backend_.reset();
}
std::shared_ptr<AllocationLease> AllocationLease::create(
    const std::shared_ptr<Backend> &backend, PoolKind kind, MemoryPool::Block block) {
    AllocationLease *lease = nullptr;
    try {
        // The lease constructor is noexcept, so this catch covers only the object
        // allocation itself; the checked-out block is still unowned there.
        lease = new AllocationLease(backend, kind, block);
    } catch (...) {
        backend->pool(kind).release(block);
        throw;
    }
    // From here the object owns the block. If the shared control block cannot be
    // allocated, shared_ptr deletes the object and its destructor releases the
    // block exactly once; this path must never release it a second time.
    return std::shared_ptr<AllocationLease>(lease);
}
std::shared_ptr<AllocationLease>
AllocationLease::device(const std::shared_ptr<Backend> &backend, size_t bytes) {
    if (!backend)
        throw std::invalid_argument("device allocation requires a CUDA backend");
    const auto block = backend->pool(PoolKind::Device).checkout(bytes);
    return create(backend, PoolKind::Device, block);
}
std::shared_ptr<AllocationLease>
AllocationLease::host(const std::shared_ptr<Backend> &backend, size_t bytes) {
    if (!backend)
        throw std::invalid_argument("pinned allocation requires a CUDA backend");
    const auto block = backend->pool(PoolKind::Host).checkout(bytes);
    return create(backend, PoolKind::Host, block);
}
} // namespace camel::tiles::cuda_detail
#endif