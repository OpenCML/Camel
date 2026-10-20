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
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The data block (mm/block.h): allocation, the atomic count, external release and the live-byte
 * accounting the collector reads. Nothing here knows what the bytes mean; arrays, Bytes and the
 * users that arrive later only see an address plus a length.
 */

#include "camel/core/mm/block.h"

#include "camel/core/mm.h"
#include "camel/core/mm/deferred.h"
#include "camel/core/slot.h"
#include "camel/utils/assert.h"

#include <algorithm>
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

void noteBytesAdded(size_t bytes) noexcept {
    auto &s           = stats();
    const size_t live = s.liveBytes.fetch_add(bytes, std::memory_order_acq_rel) + bytes;
    size_t peak       = s.peakBytes.load(std::memory_order_relaxed);
    while (live > peak &&
           !s.peakBytes.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
    }
}

void noteBytesRemoved(size_t bytes) noexcept {
    stats().liveBytes.fetch_sub(bytes, std::memory_order_acq_rel);
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
    void *block = blockSpace().alloc(blockLen, alignof(slot_t));
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
    void *block = blockSpace().alloc(blockLen, alignof(slot_t));
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

} // namespace camel::core::mm