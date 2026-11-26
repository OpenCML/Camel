// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The data block (mm/block.h): allocation, the atomic count, external release and the live-byte
 * accounting the collector reads. Nothing here knows what the bytes mean; arrays, Bytes and the
 * users that arrive later only see an address plus a length.
 */

#include "camel/core/mm/block.h"

#include "camel/core/mm.h"
#include "camel/core/mm/deferred.h"
#include "camel/core/slot.h"
#include "camel/profile/allocation.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace camel::core::mm {

namespace {

struct BlockStats {
    std::atomic<size_t> liveBytes{0};
    std::atomic<size_t> peakBytes{0};
    std::atomic<uint64_t> liveCount{0};
    std::atomic<uint64_t> creations{0};
    std::atomic<uint64_t> frees{0};
};

// Leaked: blocks can be released after static destruction begins.
BlockStats &stats() {
    static auto *value = new BlockStats();
    return *value;
}

// The trace asks where the block space's bytes go: live blocks are counted per size class, and the
// counts are copied when the live-byte peak is reached, so the answer says what was alive at the
// high-water mark rather than what is alive at exit (which is usually a tenth of it). The counters
// are the same bytes noteBytesAdded already adds, only bucketed; nothing runs while the switch is
// off. The switch is the pass parameter `std::profile block_trace=true`, which the pass hands to
// mm::enableBlockTrace (camel/core/mm.h): what a run records is decided by the program and its
// command line, not by the environment (design principles 9.3).
constexpr size_t kTraceBucket    = 256 * 1024;
constexpr size_t kTraceBuckets   = 257; // 0 .. 64 MB in 256 KB steps, the last one is "larger"
constexpr size_t kTraceReportTop = 6;

bool traceBlocks() noexcept { return camel::core::mm::blockTraceEnabled(); }

struct BlockTrace {
    std::array<std::atomic<uint64_t>, kTraceBuckets> liveBlocks{};
    std::array<std::atomic<uint64_t>, kTraceBuckets> liveBytes{};
    std::array<uint64_t, kTraceBuckets> peakBlocks{};
    std::array<uint64_t, kTraceBuckets> peakBytes{};
};

BlockTrace &trace() {
    static auto *value = new BlockTrace();
    return *value;
}

size_t traceBucketOf(size_t bytes) noexcept {
    const size_t bucket = bytes / kTraceBucket;
    return bucket < kTraceBuckets ? bucket : kTraceBuckets - 1;
}

void traceAdded(size_t bytes, bool newPeak) noexcept {
    auto &t             = trace();
    const size_t bucket = traceBucketOf(bytes);
    t.liveBlocks[bucket].fetch_add(1, std::memory_order_relaxed);
    t.liveBytes[bucket].fetch_add(bytes, std::memory_order_relaxed);
    if (newPeak) {
        // A snapshot taken while other threads are still allocating: the counts can be one block
        // off each way, which is enough to name the size classes that hold the peak.
        for (size_t i = 0; i < kTraceBuckets; ++i) {
            t.peakBlocks[i] = t.liveBlocks[i].load(std::memory_order_relaxed);
            t.peakBytes[i]  = t.liveBytes[i].load(std::memory_order_relaxed);
        }
    }
}

void traceRemoved(size_t bytes) noexcept {
    auto &t             = trace();
    const size_t bucket = traceBucketOf(bytes);
    t.liveBlocks[bucket].fetch_sub(1, std::memory_order_relaxed);
    t.liveBytes[bucket].fetch_sub(bytes, std::memory_order_relaxed);
}

void noteBytesAdded(size_t bytes) noexcept {
    auto &s           = stats();
    const size_t live = s.liveBytes.fetch_add(bytes, std::memory_order_acq_rel) + bytes;
    size_t peak       = s.peakBytes.load(std::memory_order_relaxed);
    bool newPeak      = false;
    while (live > peak &&
           !s.peakBytes.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
    }
    newPeak = live >= s.peakBytes.load(std::memory_order_relaxed);
    if (traceBlocks()) {
        traceAdded(bytes, newPeak);
    }
}

void noteBytesRemoved(size_t bytes) noexcept {
    stats().liveBytes.fetch_sub(bytes, std::memory_order_acq_rel);
    if (traceBlocks()) {
        traceRemoved(bytes);
    }
}

std::byte *alignUp(std::byte *pointer) noexcept {
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    const uintptr_t aligned = (address + (kBlockAlignment - 1)) & ~(kBlockAlignment - 1);
    return reinterpret_cast<std::byte *>(aligned);
}

} // namespace

Block::Block(BlockOwner owner, std::byte *data, size_t capacity, size_t bytes, BlockRelease ext)
    : owner_(owner), data_(data), capacity_(capacity), bytes_(bytes), external_(ext) {}

Block *Block::create(size_t bytes) {
    // At least one byte, so that data() is a valid pointer even for an empty value: a view or
    // an export of it may still carry the pointer.
    const size_t usable   = std::max<size_t>(bytes, 1);
    const size_t blockLen = usable + kBlockAlignment;
    // These bytes are live values: let the collector pace them and enforce the heap limit.
    autoSpace().checkBlockBytes(bytes);
    void *block = nullptr;
    {
        const camel::profile::AllocationScope allocation(bytes, true);
        block = blockSpace().alloc(blockLen, alignof(slot_t));
    }
    ASSERT(block != nullptr, "The block space never returns null");
    std::byte *data = alignUp(static_cast<std::byte *>(block));

    auto *blockValue = new Block(BlockOwner::Camel, data, bytes, bytes, {});
    blockValue->block_ = block;
    noteBytesAdded(bytes);
    stats().liveCount.fetch_add(1, std::memory_order_acq_rel);
    stats().creations.fetch_add(1, std::memory_order_acq_rel);
    return blockValue;
}

Block *Block::adoptExternal(void *data, size_t bytes, BlockRelease release, int device) {
    auto *block = new Block(
        BlockOwner::External, static_cast<std::byte *>(data), bytes, bytes, release);
    block->device_ = device;
    noteBytesAdded(bytes);
    stats().liveCount.fetch_add(1, std::memory_order_acq_rel);
    stats().creations.fetch_add(1, std::memory_order_acq_rel);
    return block;
}

void Block::retain() noexcept { refCount_.fetch_add(1, std::memory_order_acq_rel); }

void Block::release() noexcept {
    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        releaseNow();
    }
}

void Block::releaseFromFinalizer() noexcept {
    // The owner's finalizer runs inside the collection with the allocator's lock held;
    // deferRelease runs this after the collection, on the same thread, with the lock dropped.
    deferRelease(&Block::releaseFromFinalizerThunk, this);
}

void Block::releaseFromFinalizerThunk(void *context) noexcept {
    static_cast<Block *>(context)->release();
}

void Block::releaseNow() noexcept {
    noteBytesRemoved(capacity_);
    stats().liveCount.fetch_sub(1, std::memory_order_acq_rel);
    stats().frees.fetch_add(1, std::memory_order_acq_rel);
    if (owner_ == BlockOwner::Camel) {
        if (block_ != nullptr) {
            blockSpace().free(block_);
            block_ = nullptr;
        }
    } else if (external_.release != nullptr) {
        external_.release(external_.context);
    }
    delete this;
}

void Block::grow(size_t newBytes) {
    ASSERT(camelOwned(), "Only a Camel-owned block can grow");
    ASSERT(unique(), "A shared block cannot grow in place");
    if (newBytes <= capacity_) {
        bytes_ = newBytes;
        return;
    }
    const size_t grown    = std::max({newBytes, capacity_ * 2, size_t{64}});
    const size_t blockLen = grown + kBlockAlignment;
    autoSpace().checkBlockBytes(grown > capacity_ ? grown - capacity_ : 0);
    void *block = nullptr;
    {
        const camel::profile::AllocationScope allocation(grown, true);
        block = blockSpace().alloc(blockLen, alignof(slot_t));
    }
    ASSERT(block != nullptr, "The block space never returns null");
    std::byte *data = alignUp(static_cast<std::byte *>(block));
    if (bytes_ > 0 && data_ != nullptr) {
        std::memcpy(data, data_, bytes_);
    }
    if (block_ != nullptr) {
        blockSpace().free(block_);
    }
    noteBytesRemoved(capacity_);
    block_    = block;
    data_     = data;
    capacity_ = grown;
    bytes_    = newBytes;
    noteBytesAdded(grown);
}

size_t Block::claimPacingBytes() noexcept {
    // The block's bytes are counted at most once, whichever view is promoted or takes the
    // block over first. A growth after that adds only the bytes not counted yet.
    if (bytes_ <= pacedBytes_) {
        return 0;
    }
    const size_t added = bytes_ - pacedBytes_;
    pacedBytes_        = bytes_;
    return added;
}

bool Block::claimSweepGeneration(uint64_t generation) noexcept {
    if (sweepGeneration_ == generation) {
        return false;
    }
    sweepGeneration_ = generation;
    return true;
}

void Block::setBytes(size_t bytes) noexcept {
    ASSERT(bytes <= capacity_, "A block cannot grow through setBytes");
    bytes_ = bytes;
}

size_t blockBytesLive() noexcept { return stats().liveBytes.load(std::memory_order_acquire); }

size_t blockBytesPeak() noexcept { return stats().peakBytes.load(std::memory_order_acquire); }

uint64_t blockLiveCount() noexcept { return stats().liveCount.load(std::memory_order_acquire); }

uint64_t blockCreations() noexcept { return stats().creations.load(std::memory_order_acquire); }

uint64_t blockFrees() noexcept { return stats().frees.load(std::memory_order_acquire); }

void resetBlockStatsForTests() noexcept {
    stats().creations.store(0, std::memory_order_release);
    stats().frees.store(0, std::memory_order_release);
}

uint64_t blockTraceLiveAtPeak() noexcept {
    if (!traceBlocks()) {
        return 0;
    }
    auto &t        = trace();
    uint64_t total = 0;
    for (size_t i = 0; i < kTraceBuckets; ++i) {
        total += t.peakBlocks[i];
    }
    return total;
}
size_t blockTraceAtPeak(BlockTraceEntry *out, size_t capacity) noexcept {
    if (!traceBlocks() || out == nullptr || capacity == 0) {
        return 0;
    }
    auto &t = trace();
    // The classes are few, so the report is a selection sort over the buckets that held anything:
    // the widest class or two is what explains the high-water mark.
    std::array<size_t, kTraceBuckets> order{};
    size_t used = 0;
    for (size_t i = 0; i < kTraceBuckets; ++i) {
        if (t.peakBytes[i] != 0) {
            order[used++] = i;
        }
    }
    std::sort(order.begin(), order.begin() + static_cast<ptrdiff_t>(used), [&](size_t a, size_t b) {
        return t.peakBytes[a] > t.peakBytes[b];
    });
    const size_t written = std::min(used, capacity);
    for (size_t i = 0; i < written; ++i) {
        const size_t bucket = order[i];
        out[i] = BlockTraceEntry{bucket * kTraceBucket, t.peakBlocks[bucket], t.peakBytes[bucket]};
    }
    return written;
}

} // namespace camel::core::mm