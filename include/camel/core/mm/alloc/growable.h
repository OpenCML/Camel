// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Spaces that grow by segments.
 *
 * The graph, permanent and meta spaces hold what a program is made of (runtime graphs, types,
 * compile-time graph data), so their size follows the size of the program. Each is a list of
 * fixed segments: when no segment has room a further one is reserved, up to a configurable
 * total. Invariants the rest of the runtime relies on:
 *
 * - An object never moves and a segment is never released: every address handed out stays
 *   valid for the life of the process, as it did when the space was one block.
 * - The segment table has a fixed capacity and is only appended to, with the count published
 *   after the entry: `contains`, `free` and the iteration read it without a lock, at any time,
 *   from any thread (the collector asks `contains` while other workers allocate).
 * - Allocation never returns null. A space that reached its limit, or a host that has no
 *   memory left for a segment, throws MemoryExhausted, which names the space, the request, the
 *   usage, the limit and the setting that raises it.
 */

#pragma once

#include "allocator.h"
#include "bump_ptr.h"
#include "free_list.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <new>
#include <string>
#include <string_view>

namespace camel::core::mm {

/// A memory space cannot satisfy an allocation. Derives from std::bad_alloc so that code that
/// handles allocation failure in general keeps working; the drivers report it as
/// RuntimeDiag::MemoryExhausted.
class MemoryExhausted : public std::bad_alloc {
  public:
    /// `space` is the name of the space ("graph", "permanent", "meta", "auto"); `holds` says
    /// what lives there; `limitSetting` is the environment variable that raises the limit,
    /// empty when the limit is the memory of the host.
    MemoryExhausted(
        std::string space, std::string holds, size_t requestedBytes, size_t usedBytes,
        size_t limitBytes, std::string limitSetting);

    const char *what() const noexcept override { return message_.c_str(); }

    const std::string &space() const { return space_; }
    size_t requestedBytes() const { return requestedBytes_; }
    size_t usedBytes() const { return usedBytes_; }
    /// 0 when the space has no limit of its own and the host refused the memory.
    size_t limitBytes() const { return limitBytes_; }
    const std::string &limitSetting() const { return limitSetting_; }
    /// What to do about it, as one sentence without a final period.
    const std::string &advice() const { return advice_; }

    /// The same failure, told with what was being built when it happened ("compiling function
    /// 'f'") and the source origin of that thing (0 when unknown). The allocator knows neither;
    /// the first caller that does adds them, and an activity already set is kept.
    MemoryExhausted during(const std::string &activity, uint64_t origin = 0) const;
    const std::string &activity() const { return activity_; }
    uint64_t origin() const { return origin_; }

  private:
    void compose();

    std::string space_;
    std::string holds_;
    size_t requestedBytes_;
    size_t usedBytes_;
    size_t limitBytes_;
    std::string limitSetting_;
    std::string advice_;
    std::string activity_;
    uint64_t origin_ = 0;
    std::string message_;
};

/// How a growable space is laid out and bounded.
struct GrowableSpaceConfig {
    const char *name  = "space"; // In diagnostics and statistics.
    const char *holds = "";      // What lives there, for the diagnostic.
    // Size of the first segment. A later one is as large as everything reserved so far (the
    // space doubles), at most maxSegmentBytes, and never smaller than the request it is
    // reserved for.
    size_t segmentBytes    = 16 * MB;
    size_t maxSegmentBytes = 256 * MB;
    size_t limitBytes      = 1 * GB; // Total over all segments.
    const char *limitEnv   = "";     // Environment variable that sets limitBytes, in MB.
    // Free-list spaces only: read the size of a freed block from its header and keep no table
    // of sizes (see FreeListAllocator). For spaces that only a collector frees.
    bool sizesFromHeaders = false;
};

/// Occupancy of a growable space. `usedBytes` counts object headers.
struct SpaceUsage {
    size_t segments      = 0;
    size_t reservedBytes = 0;
    size_t usedBytes     = 0;
    size_t peakUsedBytes = 0;
    size_t allocations   = 0; // Over the life of the space.
    size_t limitBytes    = 0;
};

namespace detail {

/// Bookkeeping shared by the growable spaces: the limit, the size of the next segment and the
/// usage counters. The segment tables themselves are typed and live in the spaces.
class GrowableSpaceBase {
  public:
    static constexpr size_t kMaxSegments = 1024;

    explicit GrowableSpaceBase(const GrowableSpaceConfig &config) : config_(config) {}

    const GrowableSpaceConfig &config() const { return config_; }
    SpaceUsage usage() const;

  protected:
    /// Size of the segment to reserve for a block of `blockBytes` (header included). Throws
    /// MemoryExhausted when the limit, or the segment table, has no room for it. Called with
    /// the growth mutex held.
    size_t nextSegmentBytes(size_t blockBytes, size_t segmentCount) const;
    [[noreturn]] void throwHostExhausted(size_t blockBytes) const;

    void noteAllocated(size_t blockBytes) {
        const size_t used =
            usedBytes_.fetch_add(blockBytes, std::memory_order_relaxed) + blockBytes;
        allocations_.fetch_add(1, std::memory_order_relaxed);
        size_t peak = peakUsedBytes_.load(std::memory_order_relaxed);
        while (used > peak &&
               !peakUsedBytes_.compare_exchange_weak(peak, used, std::memory_order_relaxed)) {
        }
    }
    void noteFreed(size_t blockBytes) {
        usedBytes_.fetch_sub(blockBytes, std::memory_order_relaxed);
    }

    GrowableSpaceConfig config_;
    std::mutex growMutex_;
    std::atomic<size_t> segmentCount_{0};
    std::atomic<size_t> reservedBytes_{0};
    std::atomic<size_t> usedBytes_{0};
    std::atomic<size_t> peakUsedBytes_{0};
    std::atomic<size_t> allocations_{0};
};

} // namespace detail

/// A free-list space that grows by segments: allocation and release of single blocks.
class GrowableFreeListAllocator final : public IAllocator, public detail::GrowableSpaceBase {
  public:
    explicit GrowableFreeListAllocator(const GrowableSpaceConfig &config);

    /// Never returns null: throws MemoryExhausted.
    void *alloc(size_t size, size_t align = alignof(slot_t)) override;
    void free(void *ptr) override;
    size_t available() const override;
    bool contains(void *ptr) const override { return segmentOf(ptr) != nullptr; }
    void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const override;
    /// Whether `payload` is a block the space handed out and has not freed.
    bool isAllocated(void *payload) const {
        FreeListAllocator *segment = segmentOf(payload);
        return segment != nullptr && segment->isAllocated(payload);
    }
    /// Releases many blocks at once, each into its segment, in one pass per segment.
    void freeBulk(const std::vector<ObjectHeader *> &objects) override;

    /// Structural check of every segment (see FreeListAllocator::validate).
    bool validate() const;
    void validateFreeList(std::string_view context) const;

    size_t segmentCount() const { return segmentCount_.load(std::memory_order_acquire); }
    const FreeListAllocator &segment(size_t index) const {
        return *segments_[index].load(std::memory_order_acquire);
    }

  private:
    FreeListAllocator *segmentOf(void *ptr) const;
    void *allocSlow(size_t size, size_t align, size_t seenSegments);

    std::array<std::atomic<FreeListAllocator *>, kMaxSegments> segments_{};
    // The segment that served the last allocation: tried first, so that full segments, whose
    // free lists are walked to the end before they say no, are not asked every time.
    std::atomic<size_t> hint_{0};
};

/// A bump-pointer space that grows by segments: allocation only, nothing is ever released.
class GrowableBumpAllocator final : public IAllocator, public detail::GrowableSpaceBase {
  public:
    explicit GrowableBumpAllocator(const GrowableSpaceConfig &config);

    /// Never returns null: throws MemoryExhausted.
    void *alloc(size_t size, size_t align = alignof(slot_t)) override;
    /// The space never releases; a call is a programming error.
    void free(void *ptr) override;
    size_t available() const override;
    bool contains(void *ptr) const override;
    void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const override;

    size_t segmentCount() const { return segmentCount_.load(std::memory_order_acquire); }
    const BumpPointerAllocator &segment(size_t index) const {
        return *segments_[index].load(std::memory_order_acquire);
    }

  private:
    void *allocSlow(size_t size, size_t align, size_t seenSegments);

    std::array<std::atomic<BumpPointerAllocator *>, kMaxSegments> segments_{};
};

} // namespace camel::core::mm
