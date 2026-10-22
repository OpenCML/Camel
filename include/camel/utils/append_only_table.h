// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * An append-only "index -> entry" table whose entries never move.
 *
 * This is the storage behind the program-wide interning tables of the graph data model
 * (docs/design/graph-model, sections 3.2 and 4.3): symbols and operators are referred to by a
 * 32-bit index, and an index, once handed out, stays valid for the life of the process.
 *
 * Layout: two levels. The high bits of an index select a segment, the low bits an entry inside
 * it. Growing the table allocates a new segment and never touches the existing ones, so a
 * reference to an entry stays valid while other threads append.
 *
 * Thread-safety:
 *  - append() must be serialized by the caller (the owning table holds its own mutex).
 *  - operator[] takes no lock. It may run concurrently with append() provided the index was
 *    obtained from a completed append() and reached the reading thread through some
 *    synchronization (a mutex, a thread start, an atomic): the segment pointer is published
 *    with release/acquire ordering and the entry is fully constructed before its index exists.
 *
 * Entries are default-constructed a segment at a time and never destroyed individually; the
 * segments are released with the table.
 */

#pragma once

#include "camel/utils/assert.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace camel::utils {

template <typename Entry, size_t SegmentBits = 10, size_t MaxSegments = 4096>
class AppendOnlyTable {
  public:
    static constexpr size_t kSegmentSize = size_t{1} << SegmentBits;
    static constexpr size_t kCapacity    = kSegmentSize * MaxSegments;

    AppendOnlyTable()                                   = default;
    AppendOnlyTable(const AppendOnlyTable &)            = delete;
    AppendOnlyTable &operator=(const AppendOnlyTable &) = delete;

    ~AppendOnlyTable() {
        for (auto &slot : segments_) {
            delete[] slot.load(std::memory_order_relaxed);
        }
    }

    /// Number of entries appended so far.
    uint32_t size() const { return size_.load(std::memory_order_acquire); }

    /// Appends an entry, lets `init(entry)` fill it, and returns its index. The entry becomes
    /// visible to readers only after `init` has returned. Not thread-safe against other
    /// append() calls.
    template <typename Init> uint32_t append(Init &&init) {
        const uint32_t index = size_.load(std::memory_order_relaxed);
        ASSERT(index < kCapacity, "AppendOnlyTable is full.");
        const size_t segment = index >> SegmentBits;
        Entry *entries       = segments_[segment].load(std::memory_order_relaxed);
        if (entries == nullptr) {
            entries = new Entry[kSegmentSize]();
            segments_[segment].store(entries, std::memory_order_release);
        }
        std::forward<Init>(init)(entries[index & (kSegmentSize - 1)]);
        size_.store(index + 1, std::memory_order_release);
        return index;
    }

    /// The entry at `index`. Lock-free; see the file comment for when this may race append().
    Entry &operator[](uint32_t index) {
        ASSERT(index < size(), "AppendOnlyTable index out of range.");
        Entry *segment = segments_[index >> SegmentBits].load(std::memory_order_acquire);
        return segment[index & (kSegmentSize - 1)];
    }
    const Entry &operator[](uint32_t index) const {
        ASSERT(index < size(), "AppendOnlyTable index out of range.");
        const Entry *segment = segments_[index >> SegmentBits].load(std::memory_order_acquire);
        return segment[index & (kSegmentSize - 1)];
    }

  private:
    std::array<std::atomic<Entry *>, MaxSegments> segments_{};
    std::atomic<uint32_t> size_{0};
};

} // namespace camel::utils
