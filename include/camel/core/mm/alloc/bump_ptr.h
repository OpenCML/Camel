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
 * Created: Nov. 07, 2025
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "allocator.h"
#include "camel/utils/assert.h"
#include "camel/utils/brpred.h"
#include "camel/utils/log.h"
#include "header.h"

#ifndef NDEBUG
#include "camel/core/mm/debug_hook.h"
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring> // for std::memset
#include <memory>
#include <mutex>
#include <thread>

namespace camel::core::mm {

class BumpPointerAllocator : public IAllocator {
  public:
    BumpPointerAllocator(size_t capacity, const char *debugRegion = nullptr)
        : capacity_(capacity), debugRegion_(debugRegion) {
        // Capacity is measured in bytes and rounded up.
        size_t aligned_capacity = alignUp(capacity, alignof(slot_t));
        size_t num_units        = aligned_capacity / sizeof(slot_t);

        // Avoid value-initializing the whole arena: headers are installed explicitly on demand.
        buffer_ = std::unique_ptr<slot_t[]>(new slot_t[num_units]);
        start_  = reinterpret_cast<std::byte *>(buffer_.get());
        begin_  = start_;
        top_.store(start_, std::memory_order_relaxed);
        end_ = start_ + aligned_capacity; // Actual usable byte count.
    }

    void *alloc(size_t size, size_t align = alignof(slot_t)) override {
        ASSERT(align == alignof(slot_t), "Alignment other than 8 bytes is not supported");
        SharedOperationGuard guard(*this);

        // Round total_size up.
        // This keeps top_ aligned at all times, so we never need to call alignPointer.
        size_t total_size = alignUp(sizeof(ObjectHeader) + size, alignof(slot_t));
        EXEC_WHEN_DEBUG({
            invokePreAllocHook(PreAllocEvent{total_size, debugRegion_ ? debugRegion_ : "bump"});
        });
        std::byte *currentTop = nullptr;
        std::byte *newTop     = nullptr;

        while (true) {
            currentTop = top_.load(std::memory_order_relaxed);
            newTop     = currentTop + total_size;
            if (UNLIKELY(newTop > end_)) {
                return nullptr;
            }

            if (LIKELY(top_.compare_exchange_weak(
                    currentTop,
                    newTop,
                    std::memory_order_acq_rel,
                    std::memory_order_relaxed))) {
                break;
            }
        }

        // Install the object header and store the aligned total size.
        installHeader(currentTop, total_size);

        std::byte *result = currentTop + sizeof(ObjectHeader);

        EXEC_WHEN_DEBUG({
            CAMEL_LOG_DEBUG_S(
                "BumpPtr",
                "[{}] Allocated {} bytes ({}) from {}, obj size {}, now top at {}",
                formatAddress(start_, true),
                total_size,
                formatAddress(result, true),
                formatAddress(currentTop, true),
                size,
                formatAddress(newTop, true));

            // In debug builds, fill only the payload (result to newTop) with
            // 0xDEADBEAF and leave the header untouched.
            std::size_t bytes_to_fill = static_cast<std::size_t>(newTop - result);
            std::size_t words         = bytes_to_fill / sizeof(uint32_t);
            uint32_t *p               = reinterpret_cast<uint32_t *>(result);
            for (std::size_t i = 0; i < words; ++i) {
                p[i] = 0xDEADBEAF;
            }

            // Handle any trailing bytes if the end is not 4-byte aligned.
            std::size_t remain = bytes_to_fill % sizeof(uint32_t);
            if (remain > 0) {
                std::byte *byteTail = reinterpret_cast<std::byte *>(p + words);
                std::memset(byteTail, 0xCD, remain);
            }
        });

        EXEC_WHEN_DEBUG({
            // All bump allocations trigger the debug breakpoint, including
            // perm/meta, so alloc-step works in every region.
            invokePostAllocHook(
                AllocEvent{result, total_size, debugRegion_ ? debugRegion_ : "bump"});
        });
        return result;
    }

    void free(void *ptr) override {
        withExclusiveAccess([&] {
            ASSERT(containsUnlocked(ptr), "Pointer does not belong to this allocator");

            // Roll top_ back to the object header position.
            auto *newTop = reinterpret_cast<std::byte *>(ptr) - sizeof(ObjectHeader);

            EXEC_WHEN_DEBUG({
                auto *oldTop = top_.load(std::memory_order_relaxed);
                CAMEL_LOG_DEBUG_S(
                    "BumpPtr",
                    "[{}] Freeing object at {}, now top at {}",
                    formatAddress(start_, true),
                    formatAddress(ptr, true),
                    formatAddress(newTop, true));

                // In debug builds, fill the released space (newTop to oldTop)
                // with 0xDEADBEEF.
                std::size_t bytes_to_fill = static_cast<std::size_t>(oldTop - newTop);
                std::size_t words         = bytes_to_fill / sizeof(uint32_t);

                uint32_t *p = reinterpret_cast<uint32_t *>(newTop);
                for (std::size_t i = 0; i < words; ++i) {
                    p[i] = 0xDEADBEEF;
                }

                // Handle any trailing bytes if the end is not 4-byte aligned.
                std::size_t remain = bytes_to_fill % sizeof(uint32_t);
                if (remain > 0) {
                    std::byte *byteTail = reinterpret_cast<std::byte *>(p + words);
                    std::memset(byteTail, 0xEF, remain);
                }
            });

            top_.store(newTop, std::memory_order_release);
        });
    }

    void reset() override {
        withExclusiveAccess([&] {
            begin_ = start_;
            top_.store(start_, std::memory_order_release);
        });
    }

    // Safepoint poisoning (CAMEL_GC_POISON, see gen.h). Empties the space like reset(), but
    // first overwrites every word of the blocks it held with `poisonWord`, and then keeps
    // allocating above them while at least `minFreeBytes` remain, so the addresses of the
    // objects that were just evacuated are not handed out again until the space has been
    // walked once to its end. A pointer that still names one of them reads the poison.
    void resetPoisoned(slot_t poisonWord, size_t minFreeBytes) {
        withExclusiveAccess([&] {
            std::byte *top = top_.load(std::memory_order_relaxed);
            std::fill(
                reinterpret_cast<slot_t *>(begin_),
                reinterpret_cast<slot_t *>(top),
                poisonWord);
            if (static_cast<size_t>(end_ - top) < minFreeBytes) {
                top = start_;
            }
            begin_ = top;
            top_.store(top, std::memory_order_release);
        });
    }

    // Before the space receives the survivors of a collection in poison mode: starts over at
    // the base when fewer than `neededBytes` remain above the current top. The space must be
    // empty.
    void rewindIfShort(size_t neededBytes) {
        withExclusiveAccess([&] {
            std::byte *top = top_.load(std::memory_order_relaxed);
            ASSERT(begin_ == top, "rewindIfShort() requires an empty space");
            if (static_cast<size_t>(end_ - top) < neededBytes) {
                begin_ = start_;
                top_.store(start_, std::memory_order_release);
            }
        });
    }

    size_t capacity() const { return static_cast<size_t>(end_ - start_); }

    // Bytes held by blocks (headers included).
    size_t used() const {
        return static_cast<size_t>(top_.load(std::memory_order_acquire) - begin_);
    }

    // Whether `ptr` lies inside the blocks the space holds now. contains() is true for the
    // whole reservation, including what a reset left behind.
    bool containsAllocated(void *ptr) const {
        auto *raw = reinterpret_cast<std::byte *>(ptr);
        return raw >= begin_ && raw < top_.load(std::memory_order_acquire);
    }

    size_t available() const override {
        std::byte *top = top_.load(std::memory_order_acquire);
        return top <= end_ ? static_cast<size_t>(end_ - top) : 0;
    }

    bool contains(void *ptr) const override { return containsUnlocked(ptr); }

    std::byte *start() const { return start_; }
    std::byte *top() const { return top_.load(std::memory_order_acquire); }
    std::byte *end() const { return end_; }

    void iterateAllocated(const std::function<void(ObjectHeader *)> &visitor) const override {
        withExclusiveAccess([&] {
            std::byte *current     = begin_;
            std::byte *topSnapshot = top_.load(std::memory_order_relaxed);
            while (current < topSnapshot) {
                ObjectHeader *header = reinterpret_cast<ObjectHeader *>(current);

                // Validate the header.
                size_t obj_size = header->size();
                ASSERT(obj_size >= sizeof(ObjectHeader), "Invalid object size");
                ASSERT(obj_size % alignof(slot_t) == 0, "Object size not aligned");
                ASSERT(current + obj_size <= topSnapshot, "Object extends beyond top");

                visitor(header);
                current += obj_size;
            }
        });
    }

    void freeBulk(const std::vector<ObjectHeader *> & /*objects*/) override {
        ASSERT(false, "Bump pointer allocator does not support bulk free");
    }

    void swap(BumpPointerAllocator &other) {
        if (this == &other) {
            return;
        }

        std::scoped_lock lock(exclusiveMutex_, other.exclusiveMutex_);
        exclusiveMode_.store(true, std::memory_order_release);
        other.exclusiveMode_.store(true, std::memory_order_release);
        waitForExclusiveQuiescence();
        other.waitForExclusiveQuiescence();

        using std::swap;
        swap(capacity_, other.capacity_);
        swap(debugRegion_, other.debugRegion_);
        swap(buffer_, other.buffer_);
        swap(start_, other.start_);
        swap(begin_, other.begin_);
        std::byte *thisTop  = top_.load(std::memory_order_relaxed);
        std::byte *otherTop = other.top_.load(std::memory_order_relaxed);
        top_.store(otherTop, std::memory_order_relaxed);
        other.top_.store(thisTop, std::memory_order_relaxed);
        swap(end_, other.end_);

        exclusiveMode_.store(false, std::memory_order_release);
        other.exclusiveMode_.store(false, std::memory_order_release);
    }

  private:
    size_t capacity_;
    const char *debugRegion_{nullptr}; // Used for debug hooks; nullptr means no hook.
    std::unique_ptr<slot_t[]> buffer_;
    std::byte *start_;
    // First block the space holds. It is start_ except in poison mode (resetPoisoned).
    std::byte *begin_;
    std::atomic<std::byte *> top_;
    std::byte *end_;
    mutable std::atomic<bool> exclusiveMode_{false};
    mutable std::atomic<size_t> activeOps_{0};
    mutable std::mutex exclusiveMutex_;

    class SharedOperationGuard {
      public:
        explicit SharedOperationGuard(const BumpPointerAllocator &owner) : owner_(owner) {
            owner_.beginSharedOperation();
        }

        ~SharedOperationGuard() { owner_.endSharedOperation(); }

      private:
        const BumpPointerAllocator &owner_;
    };

    bool containsUnlocked(void *ptr) const {
        auto *raw = reinterpret_cast<std::byte *>(ptr);
        return raw >= start_ && raw < end_;
    }

    void beginSharedOperation() const {
        for (;;) {
            while (exclusiveMode_.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            activeOps_.fetch_add(1, std::memory_order_acq_rel);
            if (!exclusiveMode_.load(std::memory_order_acquire)) {
                return;
            }

            activeOps_.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    void endSharedOperation() const { activeOps_.fetch_sub(1, std::memory_order_acq_rel); }

    void waitForExclusiveQuiescence() const {
        while (activeOps_.load(std::memory_order_acquire) != 0) {
            std::this_thread::yield();
        }
    }

    template <typename Fn> void withExclusiveAccess(Fn &&fn) const {
        std::lock_guard<std::mutex> lock(exclusiveMutex_);
        exclusiveMode_.store(true, std::memory_order_release);
        waitForExclusiveQuiescence();

        try {
            std::forward<Fn>(fn)();
            exclusiveMode_.store(false, std::memory_order_release);
        } catch (...) {
            exclusiveMode_.store(false, std::memory_order_release);
            throw;
        }
    }
};

} // namespace camel::core::mm
