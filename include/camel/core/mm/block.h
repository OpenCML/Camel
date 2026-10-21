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
 * The data block: the core memory layer's general mechanism for pure data (design principles
 * 10.5; design: docs/design/arrays-memory/README.cn.md 3.2).
 *
 * A block is a leaf: it holds bytes that the type system has proved contain no reference to a
 * Camel object, so blocks cannot form cycles and can be handed to DLPack, Python or a native
 * kernel without a copy. "No reference" is the pure-data predicate of the type system
 * (type/pure.h): numbers, booleans, complex numbers, and structs and tuples made only of them.
 * Arrays of pure data are one user; Bytes is another. Long strings, table columns, memory-mapped
 * files and device memory use the same mechanism when their tasks land.
 *
 * Block data does not move and does not live in the collected heap. It is allocated from the
 * block space (mm::blockSpace(), a growable free-list space) and released when the last user
 * value that references it goes away. A block that wraps memory owned by someone else (a
 * DLPack producer, a Python bytes object, a mapped file) runs the owner's release callback
 * instead; the owner and release callback are the extension point for those users.
 *
 * The atomic count is used by parallel execution too (std::prl workers). A block becomes
 * finalizable only through a value in the collected heap; the value's finalizer queues the
 * release, and the allocator runs the queue after the collection has unlocked (see
 * mm/deferred.h).
 *
 * The live byte count takes part in the collector's pacing: a program that keeps making large
 * values that turn into garbage would otherwise never fill the small collected heap, and the
 * blocks would never be released. See mm::blockBytesLive() and gen.cpp.
 */

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>

namespace camel::core::mm {

/// The largest amount of pure data whose bytes stay inline in its collecting-heap owner (an
/// array's elements, a Bytes value). A block is allocated when the data is larger, or when a
/// stable address is needed (a view, an export). Kept in one place: the representations and the
/// baseline both read it.
inline constexpr size_t kInlineBlockBytes = 256;

/// Alignment of block data handed to kernels. The block space allocates 8-byte aligned blocks,
/// so a Camel-owned block over-allocates and aligns the payload itself.
inline constexpr size_t kBlockAlignment = 64;

enum class BlockOwner : uint8_t { Camel, External };

/// How external memory is released (DLPack's deleter, a Python bytes reference, a mapped file,
/// ...).
struct BlockRelease {
    void (*release)(void *context) noexcept = nullptr;
    void *context                           = nullptr;
};

class Block final {
  public:
    Block(const Block &)            = delete;
    Block &operator=(const Block &) = delete;

    /// A Camel-owned block with room for `bytes` bytes of data (uninitialized). Zero bytes
    /// still returns a block, so that an empty value can be a view.
    static Block *create(size_t bytes);

    /// A block over memory someone else owns. `bytes` is the usable length, which is also
    /// counted as live bytes. The release callback runs when the last reference is dropped.
    /// `device` is 0 for host memory (the only device today).
    static Block *
    adoptExternal(void *data, size_t bytes, BlockRelease release = {}, int device = 0);

    /// Adds a reference (atomic: parallel execution may share a block).
    void retain() noexcept;

    /// Drops a reference; frees the block (and its data, or runs the external release) when
    /// it was the last one. Safe on any thread.
    void release() noexcept;

    /// Releases the block from a GC finalizer, through the deferred queue: the finalizer runs
    /// with the allocator locked, and freeing a block takes the block space's lock.
    void releaseFromFinalizer() noexcept;

    std::byte *data() noexcept { return data_; }
    const std::byte *data() const noexcept { return data_; }
    /// Logical length in bytes.
    size_t bytes() const noexcept { return bytes_; }
    /// Allocated room in bytes (equal to bytes() for an adopted block).
    size_t capacity() const noexcept { return capacity_; }
    bool unique() const noexcept { return refCount_.load(std::memory_order_acquire) == 1; }
    bool camelOwned() const noexcept { return owner_ == BlockOwner::Camel; }
    size_t refCount() const noexcept { return refCount_.load(std::memory_order_acquire); }
    /// Device the data lives on (0 is the host). Device blocks get their field filled by the
    /// Device lowering later; the field exists now because DLPack carries it.
    int device() const noexcept { return device_; }

    /// Grows the logical length to `newBytes` (at least the current one). Only a Camel-owned,
    /// unique block can grow: a shared one must be replaced, because another value sees the
    /// old length. Contents are preserved. The data may move; callers re-read data().
    void grow(size_t newBytes);

    /// Claims this block's bytes for the collector's promoted-block budget and returns the
    /// bytes not claimed before (zero when the block was already counted). The collector calls
    /// it when a value holding the block is promoted, and when an elder value takes the block
    /// over (mm/alloc/allocator.h, noteBlockHeld); a block reached through several views is
    /// therefore counted once, by identity, not once per view. Called under the allocator's
    /// lock.
    size_t claimPacingBytes() noexcept;

    /// Marks the block as counted by the live-base measurement identified by `generation` and
    /// returns true the first time. A sweep therefore sums each block once even when several
    /// surviving views hold it. Called under the allocator's lock.
    bool claimSweepGeneration(uint64_t generation) noexcept;

    /// Sets the logical length without changing the allocation (used by append after grow).
    void setBytes(size_t bytes) noexcept;

  private:
    Block(BlockOwner owner, std::byte *data, size_t capacity, size_t bytes, BlockRelease ext);
    ~Block() = default;

    void releaseNow() noexcept;
    static void releaseFromFinalizerThunk(void *context) noexcept;

    std::atomic<size_t> refCount_{1};
    BlockOwner owner_ = BlockOwner::Camel;
    std::byte *data_  = nullptr;
    /// For a Camel-owned block, the block as returned by the space (data_ is aligned inside
    /// it); null for an adopted block.
    void *block_ = nullptr;
    size_t capacity_ = 0;
    size_t bytes_    = 0;
    /// Bytes already added to the promoted-block budget, and the sweep that last counted the
    /// block in the elder live base. Identity is the Block object itself (blocks never move and
    /// are not reused), so a block shared by views is counted once.
    size_t pacedBytes_             = 0;
    uint64_t sweepGeneration_      = 0;
    BlockRelease external_{};
    int device_ = 0;
};

/// Bytes of live block data (the memory pressure blocks put on the process).
size_t blockBytesLive() noexcept;
/// The high-water mark of blockBytesLive().
size_t blockBytesPeak() noexcept;
uint64_t blockLiveCount() noexcept;
/// Number of blocks ever created (a block made and released is one creation). The allocation
/// counter tests read this to prove that an operation copies or does not.
uint64_t blockCreations() noexcept;
/// Number of blocks whose last reference has been dropped (a block that is released is one
/// free). `blockCreations() - blockFrees() == blockLiveCount()`, except after the test hook
/// `resetBlockStatsForTests` (which zeroes the creation count, not the live one).
uint64_t blockFrees() noexcept;
void resetBlockStatsForTests() noexcept;

} // namespace camel::core::mm