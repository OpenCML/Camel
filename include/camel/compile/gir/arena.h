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
 * Created: Mar. 12, 2026
 * Updated: Apr. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/mm.h"
#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/mm/alloc/header.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <typeinfo>
#include <unordered_set>
#include <utility>
#include <vector>

namespace camel::compile::gir {

class GraphArena {
  public:
    enum class Region { Draft, Frozen };
    struct Metrics {
        size_t peakBytes       = 0;
        size_t wasteBytes      = 0;
        size_t blockCount      = 0;
        size_t draftFreedBytes = 0;
        size_t allocFailCount  = 0;
    };

    explicit GraphArena(size_t initialBlockBytes)
        : nextDraftBlockBytes_(std::max(initialBlockBytes, kMinBlockBytes_)),
          nextFrozenBlockBytes_(std::max(initialBlockBytes, kMinBlockBytes_)),
          draftAllocator_(this, Region::Draft), frozenAllocator_(this, Region::Frozen) {}
    ~GraphArena() {
        runTrackedDestructors(frozenTrackedDtors_);
        runTrackedDestructors(draftTrackedDtors_);
        releaseRegion(Region::Frozen);
        releaseRegion(Region::Draft);
    }

    camel::core::mm::IAllocator &allocator() { return frozenAllocator_; }
    const camel::core::mm::IAllocator &allocator() const { return frozenAllocator_; }
    camel::core::mm::IAllocator &draftAllocator() { return draftAllocator_; }
    camel::core::mm::IAllocator &frozenAllocator() { return frozenAllocator_; }

    void *alloc(size_t size, size_t align = alignof(std::max_align_t)) {
        return allocInRegion(Region::Frozen, size, align);
    }
    void *allocDraft(size_t size, size_t align = alignof(std::max_align_t)) {
        return allocInRegion(Region::Draft, size, align);
    }
    void *allocFrozen(size_t size, size_t align = alignof(std::max_align_t)) {
        return allocInRegion(Region::Frozen, size, align);
    }

    template <typename T, typename... Args> T *construct(Args &&...args) {
        void *ptr = allocFrozen(sizeof(T), alignof(T));
        return ::new (ptr) T(std::forward<Args>(args)...);
    }
    template <typename T, typename... Args> T *constructTracked(Args &&...args) {
        return constructTrackedInRegion<T>(Region::Frozen, std::forward<Args>(args)...);
    }
    template <typename T, typename... Args>
    T *constructTrackedInRegion(Region region, Args &&...args) {
        void *ptr   = allocInRegion(region, sizeof(T), alignof(T));
        T *obj      = ::new (ptr) T(std::forward<Args>(args)...);
        auto &dtors = (region == Region::Draft) ? draftTrackedDtors_ : frozenTrackedDtors_;
        dtors.push_back(
            TrackedDtor{
                .destroy   = &destroyTracked<T>,
                .ptr       = obj,
                .debugType = typeid(T).name(),
            });
        return obj;
    }

    void releaseDraftRegion() { releaseRegion(Region::Draft, false); }
    void reserveFrozenBytes(size_t expectedBytes) {
        if (availableInRegion(Region::Frozen) >= expectedBytes) {
            return;
        }
        allocateBlock(Region::Frozen, expectedBytes);
    }

    size_t used() const { return usedInRegion(Region::Draft) + usedInRegion(Region::Frozen); }
    size_t capacity() const {
        return capacityInRegion(Region::Draft) + capacityInRegion(Region::Frozen);
    }
    size_t usedDraft() const { return usedInRegion(Region::Draft); }
    size_t usedFrozen() const { return usedInRegion(Region::Frozen); }
    const Metrics &metrics() const { return metrics_; }

  private:
    class RegionAllocator : public camel::core::mm::IAllocator {
      public:
        RegionAllocator(GraphArena *arena, Region region) : arena_(arena), region_(region) {}
        void *alloc(size_t size, size_t align = alignof(slot_t)) override {
            return arena_->allocInRegion(region_, size, align);
        }
        void free(void * /*ptr*/) override {}
        void reset() override {
            if (region_ == Region::Draft) {
                arena_->releaseDraftRegion();
            }
        }
        size_t available() const override { return arena_->availableInRegion(region_); }
        bool contains(void *ptr) const override { return arena_->containsInRegion(region_, ptr); }

      private:
        GraphArena *arena_;
        Region region_;
    };
    struct TrackedDtor {
        void (*destroy)(void *) = nullptr;
        void *ptr               = nullptr;
        const char *debugType   = nullptr;
    };
    struct Block {
        std::byte *basePayload              = nullptr;
        std::byte *payload                  = nullptr;
        camel::core::mm::ObjectHeader *meta = nullptr;
        size_t used                         = 0;
        size_t rawCapacity                  = 0;
        size_t capacity                     = 0;
    };
    static constexpr size_t kMinBlockBytes_ = 32 * 1024;
    static constexpr size_t kMaxBlockBytes_ = 1024 * 1024;
    static constexpr size_t kGuardBytes_    = 64;
    static constexpr uint8_t kGuardPattern_ = 0xA5;

    static size_t alignUp(size_t value, size_t align) { return (value + align - 1) & ~(align - 1); }

    std::vector<Block> &blocksOf(Region region) {
        return region == Region::Draft ? draftBlocks_ : frozenBlocks_;
    }
    const std::vector<Block> &blocksOf(Region region) const {
        return region == Region::Draft ? draftBlocks_ : frozenBlocks_;
    }
    std::vector<camel::core::mm::ObjectHeader *> &headersOf(Region region) {
        return region == Region::Draft ? draftHeaders_ : frozenHeaders_;
    }
    size_t &nextBlockBytesOf(Region region) {
        return region == Region::Draft ? nextDraftBlockBytes_ : nextFrozenBlockBytes_;
    }

    Block &allocateBlock(Region region, size_t minBytes) {
        auto &nextBytes           = nextBlockBytesOf(region);
        const size_t requestBytes = std::max({kMinBlockBytes_, minBytes, nextBytes});
        void *basePayload =
            camel::core::mm::metaSpace().alloc(requestBytes + kGuardBytes_ * 2, alignof(slot_t));
        if (basePayload == nullptr) {
            metrics_.allocFailCount++;
            throw std::bad_alloc();
        }
        auto *header             = camel::core::mm::headerOf(static_cast<std::byte *>(basePayload));
        const size_t rawCapacity = header->objSize();
        ASSERT(
            rawCapacity >= kGuardBytes_ * 2 + minBytes,
            "GraphArena raw block capacity is smaller than requested payload+guards.");
        std::byte *payload   = static_cast<std::byte *>(basePayload) + kGuardBytes_;
        const size_t userCap = rawCapacity - kGuardBytes_ * 2;
        std::memset(static_cast<std::byte *>(basePayload), kGuardPattern_, kGuardBytes_);
        std::memset(payload + userCap, kGuardPattern_, kGuardBytes_);
        auto &blocks = blocksOf(region);
        auto &heads  = headersOf(region);
        blocks.push_back(
            Block{
                .basePayload = static_cast<std::byte *>(basePayload),
                .payload     = payload,
                .meta        = header,
                .used        = 0,
                .rawCapacity = rawCapacity,
                .capacity    = userCap});
        heads.push_back(header);
        metrics_.blockCount++;
        nextBytes = std::min(kMaxBlockBytes_, requestBytes * 2);
        return blocks.back();
    }

    void *allocInRegion(Region region, size_t size, size_t align) {
        ASSERT(align > 0 && (align & (align - 1)) == 0, "Alignment must be power-of-two.");
        auto &blocks = blocksOf(region);
        if (blocks.empty()) {
            allocateBlock(region, size + align);
        }
        auto *block        = &blocks.back();
        size_t alignedUsed = alignUp(block->used, align);
        if (alignedUsed + size > block->capacity) {
            block       = &allocateBlock(region, size + align);
            alignedUsed = alignUp(block->used, align);
        }
        ASSERT(alignedUsed + size <= block->capacity, "GraphArena block allocation overflow.");
        metrics_.wasteBytes += (alignedUsed - block->used);
        std::byte *ret     = block->payload + alignedUsed;
        block->used        = alignedUsed + size;
        metrics_.peakBytes = std::max(metrics_.peakBytes, used());
        return ret;
    }

    size_t availableInRegion(Region region) const {
        const auto &blocks = blocksOf(region);
        if (blocks.empty()) {
            return 0;
        }
        const auto &back = blocks.back();
        return back.capacity - back.used;
    }
    size_t usedInRegion(Region region) const {
        size_t total = 0;
        for (const auto &b : blocksOf(region)) {
            total += b.used;
        }
        return total;
    }
    size_t capacityInRegion(Region region) const {
        size_t total = 0;
        for (const auto &b : blocksOf(region)) {
            total += b.capacity;
        }
        return total;
    }
    bool containsInRegion(Region region, void *ptr) const {
        auto *p = static_cast<std::byte *>(ptr);
        for (const auto &b : blocksOf(region)) {
            if (p >= b.payload && p < b.payload + b.capacity) {
                return true;
            }
        }
        return false;
    }

    template <typename T> static void destroyTracked(void *ptr) {
        if (ptr != nullptr) {
            static_cast<T *>(ptr)->~T();
        }
    }
    static void runTrackedDestructors(std::vector<TrackedDtor> &dtors) {
        EXEC_WHEN_DEBUG({
            std::unordered_set<void *> seen;
            seen.reserve(dtors.size());
            for (const auto &dtor : dtors) {
                ASSERT(dtor.ptr != nullptr, "Tracked destructor contains null pointer.");
                ASSERT(
                    seen.insert(dtor.ptr).second,
                    "Tracked destructor list has duplicate object pointer.");
            }
        });
        for (auto it = dtors.rbegin(); it != dtors.rend(); ++it) {
            if (it->destroy != nullptr) {
                it->destroy(it->ptr);
            }
        }
        dtors.clear();
    }
    void releaseRegion(Region region, bool recycleMemory = true) {
        auto &blocks = blocksOf(region);
        auto &heads  = headersOf(region);
        if (region == Region::Draft) {
            metrics_.draftFreedBytes += usedInRegion(Region::Draft);
            runTrackedDestructors(draftTrackedDtors_);
            if (!recycleMemory) {
                // The Draft region is logically released after export, but returning
                // the physical memory to metaSpace would let any dangling pointer
                // immediately corrupt the free list. Defer this to GraphArena
                // destruction and reclaim it there in one place.
                return;
            }
        }
        if (!blocks.empty()) {
            auto &meta             = camel::core::mm::metaSpace();
            const bool metaHealthy = meta.validate();
            if (!metaHealthy) {
                CAMEL_LOG_WARN_S(
                    "GIR",
                    "Skip GraphArena {:p} block recycle because metaSpace is already "
                    "corrupted.",
                    static_cast<void *>(this));
                heads.clear();
                blocks.clear();
                return;
            }
            EXEC_WHEN_DEBUG({
                for (const auto &blk : blocks) {
                    auto *hdr = camel::core::mm::headerOf(blk.basePayload);
                    ASSERT(hdr != nullptr, "GraphArena block payload has null header.");
                    ASSERT(
                        blk.rawCapacity == hdr->objSize(),
                        "GraphArena block raw capacity mismatches allocator header.");
                    ASSERT(
                        blk.capacity + kGuardBytes_ * 2 == blk.rawCapacity,
                        "GraphArena block capacity/guard layout mismatch.");
                    ASSERT(
                        blk.capacity >= blk.used,
                        "GraphArena block header objSize is smaller than recorded used bytes.");
                    for (size_t i = 0; i < kGuardBytes_; ++i) {
                        ASSERT(
                            blk.basePayload[i] == static_cast<std::byte>(kGuardPattern_),
                            "GraphArena front guard corrupted (buffer underrun).");
                    }
                    for (size_t i = 0; i < kGuardBytes_; ++i) {
                        ASSERT(
                            blk.payload[blk.capacity + i] == static_cast<std::byte>(kGuardPattern_),
                            "GraphArena tail guard corrupted (buffer overflow).");
                    }
                }
            });
            for (const auto &blk : blocks) {
                meta.free(blk.basePayload);
            }
            EXEC_WHEN_DEBUG(
                ASSERT(meta.validate(), "metaSpace is corrupted after GraphArena recycle."));
        }
        if (!heads.empty()) {
            heads.clear();
        }
        blocks.clear();
    }

    std::vector<Block> draftBlocks_;
    std::vector<Block> frozenBlocks_;
    std::vector<camel::core::mm::ObjectHeader *> draftHeaders_;
    std::vector<camel::core::mm::ObjectHeader *> frozenHeaders_;
    size_t nextDraftBlockBytes_;
    size_t nextFrozenBlockBytes_;
    RegionAllocator draftAllocator_;
    RegionAllocator frozenAllocator_;
    std::vector<TrackedDtor> draftTrackedDtors_;
    std::vector<TrackedDtor> frozenTrackedDtors_;
    Metrics metrics_;
};

using graph_arena_ptr_t = std::shared_ptr<GraphArena>;

} // namespace camel::compile::gir
