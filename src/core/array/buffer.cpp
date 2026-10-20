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
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/array/buffer.h"

#include "camel/core/array/dlpack.h"
#include "camel/core/mm.h"
#include "camel/core/mm/deferred.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/slot.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <stdexcept>

namespace camel::core::array {

namespace {

struct BufferStats {
    std::atomic<size_t> liveBytes{0};
    std::atomic<size_t> peakBytes{0};
    std::atomic<uint64_t> liveCount{0};
    std::atomic<uint64_t> creations{0};
};

// Leaked: buffers can be released after static destruction begins.
BufferStats &stats() {
    static auto *value = new BufferStats();
    return *value;
}

void noteBytesAdded(size_t bytes) noexcept {
    auto &s     = stats();
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
    const uintptr_t aligned = (address + (kBufferAlignment - 1)) & ~(kBufferAlignment - 1);
    return reinterpret_cast<std::byte *>(aligned);
}

} // namespace

Buffer::Buffer(BufferOwner owner, std::byte *data, size_t capacity, size_t bytes, BufferRelease ext)
    : owner_(owner), data_(data), capacity_(capacity), bytes_(bytes), external_(ext) {}

Buffer *Buffer::create(size_t bytes) {
    // At least one byte, so that data() is a valid pointer even for an empty array: a view or
    // a DLPack export of it may still carry the pointer.
    const size_t usable   = std::max<size_t>(bytes, 1);
    const size_t blockLen = usable + kBufferAlignment;
    // These bytes are live values: let the collector pace them and enforce the heap limit.
    mm::autoSpace().checkBufferBytes(bytes);
    void *block = mm::bufferSpace().alloc(blockLen, alignof(slot_t));
    ASSERT(block != nullptr, "The buffer space never returns null");
    std::byte *data = alignUp(static_cast<std::byte *>(block));

    auto *buffer = new Buffer(BufferOwner::Camel, data, bytes, bytes, {});
    buffer->block_ = block;
    noteBytesAdded(bytes);
    stats().liveCount.fetch_add(1, std::memory_order_acq_rel);
    stats().creations.fetch_add(1, std::memory_order_acq_rel);
    return buffer;
}

Buffer *Buffer::adoptExternal(void *data, size_t bytes, BufferRelease release) {
    auto *buffer = new Buffer(
        BufferOwner::External, static_cast<std::byte *>(data), bytes, bytes, release);
    noteBytesAdded(bytes);
    stats().liveCount.fetch_add(1, std::memory_order_acq_rel);
    stats().creations.fetch_add(1, std::memory_order_acq_rel);
    return buffer;
}

void Buffer::retain() noexcept { refCount_.fetch_add(1, std::memory_order_acq_rel); }

void Buffer::release() noexcept {
    if (refCount_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        releaseNow();
    }
}

void Buffer::releaseFromFinalizer() noexcept {
    // The array's finalizer runs inside the collection with the allocator's lock held;
    // deferRelease runs this after the collection, on the same thread, with the lock dropped.
    mm::deferRelease(&Buffer::releaseFromFinalizerThunk, this);
}

void Buffer::releaseFromFinalizerThunk(void *context) noexcept {
    static_cast<Buffer *>(context)->release();
}

void Buffer::releaseNow() noexcept {
    noteBytesRemoved(capacity_);
    stats().liveCount.fetch_sub(1, std::memory_order_acq_rel);
    if (owner_ == BufferOwner::Camel) {
        if (block_) {
            mm::bufferSpace().free(block_);
            block_ = nullptr;
        }
    } else if (external_.release) {
        external_.release(external_.context);
    }
    delete this;
}

void Buffer::grow(size_t newBytes) {
    ASSERT(unique(), "A shared buffer cannot grow in place");
    if (newBytes <= capacity_) {
        bytes_ = newBytes;
        return;
    }
    const size_t grown    = std::max({newBytes, capacity_ * 2, size_t{64}});
    const size_t blockLen = grown + kBufferAlignment;
    mm::autoSpace().checkBufferBytes(grown > capacity_ ? grown - capacity_ : 0);
    void *block = mm::bufferSpace().alloc(blockLen, alignof(slot_t));
    ASSERT(block != nullptr, "The buffer space never returns null");
    std::byte *data = alignUp(static_cast<std::byte *>(block));
    if (bytes_ > 0 && data_ != nullptr) {
        std::memcpy(data, data_, bytes_);
    }
    if (block_) {
        mm::bufferSpace().free(block_);
    }
    noteBytesRemoved(capacity_);
    block_    = block;
    data_     = data;
    capacity_ = grown;
    bytes_    = newBytes;
    noteBytesAdded(grown);
}

void Buffer::setBytes(size_t bytes) noexcept {
    ASSERT(bytes <= capacity_, "Buffer length exceeds its capacity");
    bytes_ = bytes;
}

size_t bufferBytesLive() noexcept { return stats().liveBytes.load(std::memory_order_acquire); }

size_t bufferBytesPeak() noexcept { return stats().peakBytes.load(std::memory_order_acquire); }

uint64_t bufferLiveCount() noexcept { return stats().liveCount.load(std::memory_order_acquire); }

uint64_t bufferCreations() noexcept { return stats().creations.load(std::memory_order_acquire); }

void resetBufferStatsForTests() noexcept {
    auto &s = stats();
    s.peakBytes.store(s.liveBytes.load(std::memory_order_acquire), std::memory_order_release);
    s.creations.store(0, std::memory_order_release);
}

bool debugRunBufferSelfTest() {
    using camel::core::type::ArrayType;
    using camel::core::type::Type;

    const size_t bytesBefore      = bufferBytesLive();
    const uint64_t creationsBefore = bufferCreations();

    // A small numeric array lives inline in its array object: no buffer, no buffer bytes.
    Array *small = Array::create(mm::autoSpace(), ArrayType::create(Type::Int64()), 3);
    if (!small->isCompact() || small->buffer() != nullptr || small->byteSize() != 24) {
        throw std::runtime_error("buffer self-test: an int[3] is not inline");
    }
    for (int i = 0; i < 3; ++i) {
        small->set<rtdata::Int64>(i, static_cast<rtdata::Int64>(i + 1));
    }
    if (small->get<rtdata::Int64>(2) != 3 || bufferBytesLive() != bytesBefore) {
        throw std::runtime_error("buffer self-test: an inline array counts buffer bytes");
    }

    // A view promotes the inline array once, and both share one buffer.
    Array *viewed = Array::view(small, std::span<const int64_t>(small->shape(), 1));
    if (small->buffer() == nullptr || viewed->buffer() != small->buffer() ||
        small->buffer()->refCount() < 2 || bufferCreations() != creationsBefore + 1 ||
        bufferBytesLive() != bytesBefore + 24) {
        throw std::runtime_error("buffer self-test: a view did not share the promoted buffer");
    }
    if (viewed->get<rtdata::Int64>(2) != 3) {
        throw std::runtime_error("buffer self-test: a view does not read the source");
    }

    // A large array is buffer backed from the start.
    Array *big = Array::create(mm::autoSpace(), ArrayType::create(Type::Int64()), 100);
    if (!big->isCompact() || big->buffer() == nullptr || big->byteSize() != 800) {
        throw std::runtime_error("buffer self-test: an int[100] is not buffer backed");
    }
    for (size_t i = 0; i < 100; ++i) {
        big->set<rtdata::Int64>(i, static_cast<rtdata::Int64>(i));
    }

    // DLPack export is zero-copy: same data address, buffer retained by the managed tensor.
    DLManagedTensor *managed = toDLPack(big);
    if (managed->dl_tensor.data != big->rawData() || managed->dl_tensor.ndim != 1 ||
        managed->dl_tensor.shape[0] != 100 || big->buffer()->refCount() < 2) {
        throw std::runtime_error("buffer self-test: DLPack export copied the data");
    }

    // Import adopts the same address (a second Buffer over the producer's memory).
    Array *imported = fromDLPack(managed, mm::autoSpace());
    if (imported->rawData() != big->rawData() || imported->size() != 100 ||
        imported->get<rtdata::Int64>(42) != 42) {
        throw std::runtime_error("buffer self-test: DLPack import did not share the data");
    }

    // The arrays stay alive for the rest of the process (they are GC objects); the pass is a
    // diagnostic, not a unit test with teardown.
    return true;
}

} // namespace camel::core::array