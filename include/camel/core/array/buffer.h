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

/*
 * The numeric data buffer (design: docs/design/arrays-memory/README.cn.md 3.2).
 *
 * A buffer is a leaf: it holds numbers only, never a reference to a Camel object (design
 * principles 10.5). That is what makes reference counting safe (no cycles) and what lets a
 * data pointer be handed to DLPack, PyTorch or a native kernel without a copy.
 *
 * Buffer data does not move and does not live in the collected heap. It is allocated from the
 * buffer space (mm::bufferSpace(), a growable free-list space) and released when the last
 * array value that references it goes away. A buffer that wraps memory owned by someone else
 * (a DLPack producer, a Python capsule) runs the owner's release callback instead.
 *
 * The atomic count is used by parallel execution too (std::prl workers). A buffer becomes
 * finalizable only through an array value in the collected heap; the array's finalizer queues
 * the release, and the allocator runs the queue after the collection has unlocked (see
 * mm/deferred.h).
 *
 * The live byte count takes part in the collector's pacing: a program that keeps making large
 * arrays that turn into garbage would otherwise never fill the small collected heap, and the
 * buffers would never be released. See mm::bufferBytesLive() and gen.cpp.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace camel::core::array {

/// The largest small numeric array whose data stays inline in its GC object. Chosen so that
/// `alloc_small`-scale logic programs never allocate a buffer for a vector of a few dozen
/// numbers, while a real tensor slot does. Kept in one place: the representation and the
/// baseline both read it.
inline constexpr size_t kInlineArrayBytes = 256;

/// Alignment of buffer data handed to kernels. The buffer space allocates 8-byte aligned
/// blocks, so a Camel-owned buffer over-allocates and aligns the payload itself.
inline constexpr size_t kBufferAlignment = 64;

enum class BufferOwner : uint8_t { Camel, External };

/// How external memory is released (DLPack's deleter, a Python capsule's destructor, ...).
struct BufferRelease {
    void (*release)(void *context) noexcept = nullptr;
    void *context                           = nullptr;
};

class Buffer final {
  public:
    Buffer(const Buffer &)            = delete;
    Buffer &operator=(const Buffer &) = delete;

    /// A Camel-owned buffer with room for `bytes` bytes of data (uninitialized). Zero bytes
    /// still returns a buffer, so that an empty array can be a view.
    static Buffer *create(size_t bytes);

    /// A buffer over memory someone else owns. `bytes` is the usable length, which is also
    /// counted as live bytes. The release callback runs when the last reference is dropped.
    static Buffer *adoptExternal(void *data, size_t bytes, BufferRelease release = {});

    /// Adds a reference (atomic: parallel execution may share a buffer).
    void retain() noexcept;

    /// Drops a reference; frees the buffer (and its data, or runs the external release) when
    /// it was the last one. Safe on any thread.
    void release() noexcept;

    /// Releases the block from a GC finalizer, through the deferred queue: the finalizer runs
    /// with the allocator locked, and freeing a buffer takes the buffer space's lock.
    void releaseFromFinalizer() noexcept;

    std::byte *data() noexcept { return data_; }
    const std::byte *data() const noexcept { return data_; }
    /// Logical length in bytes.
    size_t bytes() const noexcept { return bytes_; }
    /// Allocated room in bytes (equal to bytes() for an adopted buffer).
    size_t capacity() const noexcept { return capacity_; }
    bool unique() const noexcept { return refCount_.load(std::memory_order_acquire) == 1; }
    bool camelOwned() const noexcept { return owner_ == BufferOwner::Camel; }
    size_t refCount() const noexcept { return refCount_.load(std::memory_order_acquire); }
    /// Device the data lives on (0 is the host). The alias lowering for Device arrives later;
    /// the field exists now because DLPack carries it.
    int device() const noexcept { return device_; }

    /// Grows the logical length to `newBytes` (at least the current one). Only a Camel-owned,
    /// unique buffer can grow: a shared one must be replaced, because another array value sees
    /// the old length. Contents are preserved. The data may move; callers re-read data().
    void grow(size_t newBytes);

    /// Sets the logical length without changing the allocation (used by append after grow).
    void setBytes(size_t bytes) noexcept;

  private:
    Buffer(BufferOwner owner, std::byte *data, size_t capacity, size_t bytes, BufferRelease ext);
    ~Buffer() = default;

    void releaseNow() noexcept;
    static void releaseFromFinalizerThunk(void *context) noexcept;

    std::atomic<size_t> refCount_{1};
    BufferOwner owner_ = BufferOwner::Camel;
    std::byte *data_   = nullptr;
    /// For a Camel-owned buffer, the block as returned by the space (data_ is aligned inside
    /// it); null for an adopted buffer.
    void *block_ = nullptr;
    size_t capacity_ = 0;
    size_t bytes_    = 0;
    BufferRelease external_{};
    int device_ = 0;
};

/// Bytes of live buffer data (the memory pressure buffers put on the process).
size_t bufferBytesLive() noexcept;
/// The high-water mark of bufferBytesLive().
size_t bufferBytesPeak() noexcept;
uint64_t bufferLiveCount() noexcept;
/// Number of buffers ever created (a buffer made and released is one creation). The allocation
/// counter tests read this to prove that an operation copies or does not.
uint64_t bufferCreations() noexcept;
void resetBufferStatsForTests() noexcept;

/// Debug self-test (`std::gc::buffers`): inline arrays, promotion to a buffer, views and DLPack
/// sharing. Throws when an invariant is broken; returns true otherwise.
bool debugRunBufferSelfTest();

} // namespace camel::core::array