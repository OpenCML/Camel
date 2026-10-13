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
 * Created: Oct. 11, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Spaces that grow by segments: see growable.h for the invariants.
 */

#include "camel/core/mm/alloc/growable.h"

#include <algorithm>
#include <format>

namespace camel::core::mm {

namespace {

std::string megabytes(size_t bytes) {
    if (bytes < KB) {
        return std::format("{} bytes", bytes);
    }
    if (bytes < MB) {
        return std::format("{} KB", (bytes + KB - 1) / KB);
    }
    return std::format("{:.1f} MB", static_cast<double>(bytes) / static_cast<double>(MB));
}

size_t blockBytesFor(size_t payloadBytes) {
    return alignUp(sizeof(ObjectHeader) + payloadBytes, alignof(slot_t));
}

} // namespace

MemoryExhausted::MemoryExhausted(
    std::string space, std::string holds, size_t requestedBytes, size_t usedBytes,
    size_t limitBytes, std::string limitSetting)
    : space_(std::move(space)), holds_(std::move(holds)), requestedBytes_(requestedBytes),
      usedBytes_(usedBytes), limitBytes_(limitBytes), limitSetting_(std::move(limitSetting)) {
    compose();
}

MemoryExhausted MemoryExhausted::during(const std::string &activity, uint64_t origin) const {
    MemoryExhausted told = *this;
    if (told.activity_.empty()) {
        told.activity_ = activity;
    }
    if (told.origin_ == 0) {
        told.origin_ = origin;
    }
    told.compose();
    return told;
}

void MemoryExhausted::compose() {
    const std::string what = holds_.empty() ? std::string() : std::format(" ({})", holds_);
    const std::string while_ =
        activity_.empty() ? std::string() : std::format(" while {}", activity_);
    if (limitBytes_ != 0) {
        advice_  = limitSetting_.empty()
                       ? std::string("the limit is fixed")
                       : std::format(
                            "set {}=<megabytes> to raise the limit (it is {} now)",
                            limitSetting_,
                            limitBytes_ / MB);
        message_ = std::format(
            "The {} space{} is exhausted{}: an allocation of {} does not fit, {} of the limit "
            "of {} are in use; {}",
            space_,
            what,
            while_,
            megabytes(requestedBytes_),
            megabytes(usedBytes_),
            megabytes(limitBytes_),
            advice_);
    } else {
        advice_  = "the host has no more memory to give; the space has no limit of its own";
        message_ = std::format(
            "The {} space{} is exhausted{}: the host refused an allocation of {} with {} in "
            "use; {}",
            space_,
            what,
            while_,
            megabytes(requestedBytes_),
            megabytes(usedBytes_),
            advice_);
    }
}

namespace detail {

SpaceUsage GrowableSpaceBase::usage() const {
    SpaceUsage usage;
    usage.segments      = segmentCount_.load(std::memory_order_acquire);
    usage.reservedBytes = reservedBytes_.load(std::memory_order_relaxed);
    usage.usedBytes     = usedBytes_.load(std::memory_order_relaxed);
    usage.peakUsedBytes = peakUsedBytes_.load(std::memory_order_relaxed);
    usage.allocations   = allocations_.load(std::memory_order_relaxed);
    usage.limitBytes    = config_.limitBytes;
    return usage;
}

size_t GrowableSpaceBase::nextSegmentBytes(size_t blockBytes, size_t segmentCount) const {
    const size_t reserved = reservedBytes_.load(std::memory_order_relaxed);
    const size_t room     = config_.limitBytes > reserved ? config_.limitBytes - reserved : 0;
    if (blockBytes > room || segmentCount >= kMaxSegments) {
        throw MemoryExhausted(
            config_.name,
            config_.holds,
            blockBytes,
            usedBytes_.load(std::memory_order_relaxed),
            config_.limitBytes,
            config_.limitEnv);
    }
    // Double the space, within the bounds of one segment and of the limit.
    size_t bytes = std::max(config_.segmentBytes, reserved);
    bytes        = std::min(bytes, std::max(config_.maxSegmentBytes, config_.segmentBytes));
    bytes        = std::max(bytes, blockBytes);
    // Every term is a multiple of the slot size: the configured sizes, block sizes, and the
    // room, which is the limit minus earlier segments.
    return std::min(bytes, room);
}

void GrowableSpaceBase::throwHostExhausted(size_t blockBytes) const {
    throw MemoryExhausted(
        config_.name,
        config_.holds,
        blockBytes,
        usedBytes_.load(std::memory_order_relaxed),
        0,
        {});
}

} // namespace detail

// ---------------------------------------------------------------------------------------------
// GrowableFreeListAllocator
// ---------------------------------------------------------------------------------------------

GrowableFreeListAllocator::GrowableFreeListAllocator(const GrowableSpaceConfig &config)
    : GrowableSpaceBase(config) {}

void *GrowableFreeListAllocator::alloc(size_t size, size_t align) {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    if (LIKELY(count != 0)) {
        const size_t hint = std::min(hint_.load(std::memory_order_relaxed), count - 1);
        if (void *ptr = segments_[hint].load(std::memory_order_acquire)->alloc(size, align)) {
            noteAllocated(headerOf(ptr)->size());
            return ptr;
        }
        // Newest first: older segments are the fullest.
        for (size_t i = count; i-- > 0;) {
            if (i == hint) {
                continue;
            }
            if (void *ptr = segments_[i].load(std::memory_order_acquire)->alloc(size, align)) {
                hint_.store(i, std::memory_order_relaxed);
                noteAllocated(headerOf(ptr)->size());
                return ptr;
            }
        }
    }
    return allocSlow(size, align, count);
}

void *GrowableFreeListAllocator::allocSlow(size_t size, size_t align, size_t seenSegments) {
    std::lock_guard<std::mutex> lock(growMutex_);
    // Another thread may have grown the space while this one waited for the mutex.
    size_t count = segmentCount_.load(std::memory_order_acquire);
    for (size_t i = seenSegments; i < count; ++i) {
        if (void *ptr = segments_[i].load(std::memory_order_acquire)->alloc(size, align)) {
            hint_.store(i, std::memory_order_relaxed);
            noteAllocated(headerOf(ptr)->size());
            return ptr;
        }
    }

    const size_t blockBytes    = blockBytesFor(size);
    const size_t segmentBytes  = nextSegmentBytes(blockBytes, count);
    FreeListAllocator *segment = nullptr;
    try {
        segment = new FreeListAllocator(segmentBytes, config_.name, config_.sizesFromHeaders);
    } catch (const std::bad_alloc &) {
        throwHostExhausted(blockBytes);
    }
    void *ptr = segment->alloc(size, align);
    ASSERT(ptr != nullptr, "A segment reserved for an allocation must hold it.");
    // Publish the entry before the count: a reader that sees the count sees the segment.
    segments_[count].store(segment, std::memory_order_release);
    reservedBytes_.fetch_add(segmentBytes, std::memory_order_relaxed);
    segmentCount_.store(count + 1, std::memory_order_release);
    hint_.store(count, std::memory_order_relaxed);
    noteAllocated(headerOf(ptr)->size());
    return ptr;
}

FreeListAllocator *GrowableFreeListAllocator::segmentOf(void *ptr) const {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    for (size_t i = count; i-- > 0;) {
        FreeListAllocator *segment = segments_[i].load(std::memory_order_acquire);
        if (segment->contains(ptr)) {
            return segment;
        }
    }
    return nullptr;
}

void GrowableFreeListAllocator::free(void *ptr) {
    if (UNLIKELY(!ptr)) {
        return;
    }
    FreeListAllocator *segment = segmentOf(ptr);
    ASSERT(segment != nullptr, "Free pointer was not allocated by this space.");
    if (UNLIKELY(!segment)) {
        return;
    }
    const size_t blockBytes = headerOf(ptr)->size();
    segment->free(ptr);
    noteFreed(blockBytes);
}

void GrowableFreeListAllocator::freeBulk(const std::vector<ObjectHeader *> &objects) {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    if (count == 1) {
        size_t bytes = 0;
        for (ObjectHeader *header : objects) {
            bytes += header->size();
        }
        segments_[0].load(std::memory_order_acquire)->freeBulk(objects);
        noteFreed(bytes);
        return;
    }
    std::vector<std::vector<ObjectHeader *>> perSegment(count);
    size_t bytes = 0;
    for (ObjectHeader *header : objects) {
        for (size_t i = count; i-- > 0;) {
            if (segments_[i].load(std::memory_order_acquire)->contains(header)) {
                perSegment[i].push_back(header);
                bytes += header->size();
                break;
            }
        }
    }
    for (size_t i = 0; i < count; ++i) {
        if (!perSegment[i].empty()) {
            segments_[i].load(std::memory_order_acquire)->freeBulk(perSegment[i]);
        }
    }
    noteFreed(bytes);
}

size_t GrowableFreeListAllocator::available() const {
    const size_t reserved = reservedBytes_.load(std::memory_order_relaxed);
    const size_t used     = usedBytes_.load(std::memory_order_relaxed);
    return reserved > used ? reserved - used : 0;
}

void GrowableFreeListAllocator::iterateAllocated(
    const std::function<void(ObjectHeader *)> &visitor) const {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) {
        segments_[i].load(std::memory_order_acquire)->iterateAllocated(visitor);
    }
}

bool GrowableFreeListAllocator::validate() const {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) {
        if (!segments_[i].load(std::memory_order_acquire)->validate()) {
            return false;
        }
    }
    return true;
}

void GrowableFreeListAllocator::validateFreeList(std::string_view context) const {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) {
        segments_[i].load(std::memory_order_acquire)->validateFreeList(context);
    }
}

// ---------------------------------------------------------------------------------------------
// GrowableBumpAllocator
// ---------------------------------------------------------------------------------------------

GrowableBumpAllocator::GrowableBumpAllocator(const GrowableSpaceConfig &config)
    : GrowableSpaceBase(config) {}

void *GrowableBumpAllocator::alloc(size_t size, size_t align) {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    if (LIKELY(count != 0)) {
        // Only the newest segment is asked: the tail an older one has left is given up, which
        // costs at most one object's worth of room per segment.
        void *ptr = segments_[count - 1].load(std::memory_order_acquire)->alloc(size, align);
        if (LIKELY(ptr != nullptr)) {
            noteAllocated(headerOf(ptr)->size());
            return ptr;
        }
    }
    return allocSlow(size, align, count);
}

void *GrowableBumpAllocator::allocSlow(size_t size, size_t align, size_t seenSegments) {
    std::lock_guard<std::mutex> lock(growMutex_);
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    if (count != seenSegments) {
        if (void *ptr = segments_[count - 1].load(std::memory_order_acquire)->alloc(size, align)) {
            noteAllocated(headerOf(ptr)->size());
            return ptr;
        }
    }

    const size_t blockBytes       = blockBytesFor(size);
    const size_t segmentBytes     = nextSegmentBytes(blockBytes, count);
    BumpPointerAllocator *segment = nullptr;
    try {
        segment = new BumpPointerAllocator(segmentBytes, config_.name);
    } catch (const std::bad_alloc &) {
        throwHostExhausted(blockBytes);
    }
    void *ptr = segment->alloc(size, align);
    ASSERT(ptr != nullptr, "A segment reserved for an allocation must hold it.");
    segments_[count].store(segment, std::memory_order_release);
    reservedBytes_.fetch_add(segmentBytes, std::memory_order_relaxed);
    segmentCount_.store(count + 1, std::memory_order_release);
    noteAllocated(headerOf(ptr)->size());
    return ptr;
}

void GrowableBumpAllocator::free(void * /*ptr*/) {
    ASSERT(false, "A growable bump space does not release single objects.");
}

size_t GrowableBumpAllocator::available() const {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    return count == 0 ? 0 : segments_[count - 1].load(std::memory_order_acquire)->available();
}

bool GrowableBumpAllocator::contains(void *ptr) const {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    for (size_t i = count; i-- > 0;) {
        if (segments_[i].load(std::memory_order_acquire)->contains(ptr)) {
            return true;
        }
    }
    return false;
}

void GrowableBumpAllocator::iterateAllocated(
    const std::function<void(ObjectHeader *)> &visitor) const {
    const size_t count = segmentCount_.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) {
        segments_[i].load(std::memory_order_acquire)->iterateAllocated(visitor);
    }
}

} // namespace camel::core::mm
