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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/bump_ptr.h"
#include "alloc/free_list.h"
#include "alloc/large_obj.h"
#include "camel/core/rtdata/base.h"

#include "camel/utils/assert.h"
#include "camel/utils/brpred.h"

#include <mutex>

// ============================================================================
// Generational GC memory layout overview
// ============================================================================
//
//  ┌────────────────────────────────────────────────────┐
//  │                 Young Generation                   │  ← Where new objects live; copying
//  collection ├────────────────────────────────────────────────────┤ │
//  ┌──────────────────────────────────────────────┐  │ │  │  birthSpace_ (Birth Space / Eden) │  │
//  ← Fast allocation for new objects │  │  ████████████████████████████████████████    │  │ [Minor
//  GC source] │  │  (Larger, e.g., 8MB)                         │  │ │
//  └──────────────────────────────────────────────┘  │ │ │ │  ┌────────────────────────────┐ │ │  │
//  havenSpace_ (To Space)     │                    │  ← Survivor To │  │  ████████████████ │ │
//  [Minor GC destination] │  │  (Smaller, e.g., 1MB)      │                    │ │
//  └────────────────────────────┘                    │ │           ⇅ (Swap during Minor GC) │ │
//  ┌────────────────────────────┐                    │ │  │ cacheSpace_ (From Space)   │ │  ←
//  Survivor From │  │  ████████████████          │                    │     [Minor GC source] │  │
//  (Equal to havenSpace_)    │                    │ │  └────────────────────────────┘ │
//  └────────────────────────────────────────────────────┘
//           │ (age >= threshold)
//           │ Promotion
//           ↓
//  ┌────────────────────────────────────┐
//  │          Elder Generation          │  ← Long-lived object storage
//  ├────────────────────────────────────┤
//  │  elderGenSpace_                    │  ← FreeList allocator
//  │  ┌─────┐ ┌────┐ ┌──────┐ ┌─────┐   │     [Mark-sweep]
//  │  │ Obj │ │Free│ │ Obj  │ │ Obj │   │
//  │  └─────┘ └────┘ └──────┘ └─────┘   │     Fragmentation via free list
//  │  ████████████████████████████████  │     (linked free list)
//  │  (Dynamic sizing)                  │
//  └────────────────────────────────────┘
//
//  ┌────────────────────────────────────┐
//  │         Large Object Space         │  ← Dedicated region for very large objects
//  ├────────────────────────────────────┤
//  │  largeObjSpace_                    │  ← Separate allocator
//  │  ┌────────────────────────────┐    │     [Avoids copy cost]
//  │  │   Large Object 1           │    │
//  │  │   (size > threshold)       │    │
//  │  └────────────────────────────┘    │
//  │  ┌────────────────────────────┐    │
//  │  │   Large Object 2           │    │
//  │  └────────────────────────────┘    │
//  │  ████████████████████████████████  │
//  │  (Dynamic sizing)                  │
//  └────────────────────────────────────┘
//
// ============================================================================
// Young-generation subspace size comparison (typical configuration)
// ============================================================================
//
//    birthSpace_:    ████████████████████████ (8MB)              ← Largest Birth region
//    havenSpace_:    ███ (1MB)                                   ← Smaller Haven
//    cacheSpace_:    ███ (1MB)                                   ← Same as Haven
//    elderGenSpace_: ██████████████████████████████████████████████████ (16MB)
//
//  Design rationale:
//  - Large birthSpace_: most objects die young; ample room reduces GC frequency
//  - Small havenSpace_/cacheSpace_: few survivors per GC (often under 10%); no need for huge
//  To/From
//  - Typical ratio Birth : Haven : Haven = 8 : 1 : 1 (tunable)
//
// ============================================================================

// ============================================================================
// Object lifetime flow
// ============================================================================
//
//   [new Object]
//        │
//        ├─→ (size > largeObjThreshold_) ──→ largeObjSpace_
//        │                                      │
//        │                                      ↓
//        │                                  [Major GC sweep]
//        │
//        └─→ (small object) ──→ birthSpace_
//                           │
//                           ↓
//                      [Minor GC]
//                           │
//              ┌────────────┴────────────┐
//              │                         │
//       (live / reachable)         (dead / unreachable)
//              │                         │
//              ↓                         ↓
//         forward() (age+=1)        [reclaimed automatically]
//              │
//    ┌─────────┴─────────┐
//    │                   │
// (age < threshold)  (age >= threshold)
//    │                   │
//    ↓                   ↓
// havenSpace_      elderGenSpace_
//  (stay in young gen) (promote to old gen)
//    │                   │
//    └────────┬──────────┘
//             ↓
//        [Major GC sweep]
//
// ============================================================================

// ============================================================================
// Minor GC detailed flow
// ============================================================================
//
//  State before GC:
//  ┌─────────────────┐
//  │  birthSpace_    │  [████████████████████████████] ← full; triggers Minor GC
//  └─────────────────┘
//  ┌─────────────────┐
//  │  havenSpace_    │  [███░░░] ← To space (live objects)
//  └─────────────────┘
//  ┌─────────────────┐
//  │  cacheSpace_    │  [░░░░░░] ← From space (empty)
//  └─────────────────┘
//
//  Step 1: swap havenSpace_ ↔ cacheSpace_
//  ┌─────────────────┐
//  │  birthSpace_    │  [████████████████████████████]
//  └─────────────────┘
//  ┌─────────────────┐
//  │  havenSpace_    │  [░░░░░░] ← new To (was From)
//  └─────────────────┘
//  ┌─────────────────┐
//  │  cacheSpace_    │  [███░░░] ← new From (was To)
//  └─────────────────┘
//
//  Step 2: copy live/reachable objects (roots drive traversal; reachability implied)
//  ┌─────────────────┐
//  │  birthSpace_    │  [████████████████████████████] → scan live objects
//  └─────────────────┘       ↓
//  ┌─────────────────┐       ↓ (copy move, age+=1)
//  │  havenSpace_    │  [██░░░░] ← survivors copied here
//  └─────────────────┘       ↑ (copy move, age+=1)
//  ┌─────────────────┐       ↑
//  │  cacheSpace_    │  [███░░░] → scan live objects
//  └─────────────────┘
//
//  Step 3: bulk-reset birthSpace_ and cacheSpace_
//  ┌─────────────────┐
//  │  birthSpace_    │  [░░░░░░░░░░░░░░░░░░░░░░░░░░░░] ← cleared
//  └─────────────────┘
//  ┌─────────────────┐
//  │  havenSpace_    │  [██░░░░] ← only survivors remain
//  └─────────────────┘
//  ┌─────────────────┐
//  │  cacheSpace_    │  [░░░░░░] ← cleared
//  └─────────────────┘
//
// ============================================================================

namespace camel::core::mm {

namespace rtdata = camel::core::rtdata;

class GenerationalAllocatorWithGC : public IAllocator {
  public:
    struct Config {
        size_t birthSize;
        size_t havenSize;
        size_t elderGenSize;
        size_t promotionAgeThreshold;
        size_t largeObjThreshold;
        float minorGCTriggerRatio;
        float majorGCTriggerRatio;
    };

    GenerationalAllocatorWithGC(const Config &config)
        : birthSpace_(config.birthSize, "auto.birth"), havenSpace_(config.havenSize, "auto.haven"),
          cacheSpace_(config.havenSize, "auto.cache"),
          elderGenSpace_(config.elderGenSize, "auto.elder"), largeObjSpace_("auto.large"),
          promotionAgeThreshold_(config.promotionAgeThreshold),
          largeObjThreshold_(config.largeObjThreshold),
          minorGCTriggerRatio_(config.minorGCTriggerRatio),
          majorGCTriggerRatio_(config.majorGCTriggerRatio) {}

    void *alloc(size_t payloadSize, size_t align = alignof(slot_t)) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocUnlocked(payloadSize, align);
    }

    void free(void *ptr) override {
        (void)ptr;
        ASSERT(false, "GenerationalAllocatorWithGC does not support manual free");
    }

    void setObjectRootSet(std::vector<rtdata::Object *> *rootSet) {
        std::lock_guard<std::mutex> lock(mutex_);
        rootObjectSet_ = rootSet;
    }

    void recordOldToYoungRef(void *oldObj, void *youngObj) {
        (void)youngObj;
        std::lock_guard<std::mutex> lock(mutex_);
        ObjectHeader *header = headerOf(oldObj);
        rememberedSet_.insert(header);
    }

    // Minor GC: collect the young generation (Birth + From).
    void minorGC() {
        std::lock_guard<std::mutex> lock(mutex_);
        minorGCUnlocked();
    }

    // Debugger / profiler: expose sub-regions (e.g. memory visualization).
    const BumpPointerAllocator &birthSpace() const { return birthSpace_; }
    const BumpPointerAllocator &havenSpace() const { return havenSpace_; }
    const BumpPointerAllocator &cacheSpace() const { return cacheSpace_; }
    const FreeListAllocator &elderGenSpace() const { return elderGenSpace_; }
    const LargeObjectAllocator &largeObjSpace() const { return largeObjSpace_; }

    // Major GC: collect the entire heap.
    void majorGC() {
        std::lock_guard<std::mutex> lock(mutex_);
        majorGCUnlocked();
    }

  private:
    void *allocUnlocked(size_t payloadSize, size_t align = alignof(slot_t)) {
        ASSERT(align == alignof(slot_t), "Alignment other than 8 bytes is not supported");

        // Large objects use the large-object allocator directly.
        if (UNLIKELY(payloadSize > largeObjThreshold_)) {
            void *ptr = largeObjSpace_.alloc(payloadSize, align);
            if (UNLIKELY(!ptr)) {
                majorGCUnlocked();
                ptr = largeObjSpace_.alloc(payloadSize, align);
                if (!ptr)
                    throw std::bad_alloc();
            }
            auto *header = headerOf(ptr);
            header->setRegion(AllocRegion::LargeObj);
            return ptr;
        }

        // Try allocating in the birth space first.
        void *ptr = birthSpace_.alloc(payloadSize, align);
        if (UNLIKELY(!ptr)) {
            minorGCUnlocked();
            ptr = birthSpace_.alloc(payloadSize, align);
            if (UNLIKELY(!ptr)) {
                majorGCUnlocked();
                ptr = birthSpace_.alloc(payloadSize, align);
                if (!ptr)
                    throw std::bad_alloc();
            }
        }

        auto *header = headerOf(ptr);
        header->setRegion(AllocRegion::YoungGen);
        return ptr;
    }

    void minorGCUnlocked() {
        if (inGC_)
            return; // Reentrancy guard
        inGC_ = true;

        try {
            // 1. Swap Cache and Haven
            cacheSpace_.swap(havenSpace_);
            havenSpace_.reset(); // Clear the new Haven (To) space

            // 2. Forward young-gen objects referenced from roots
            for (rtdata::Object *&rootObj : *rootObjectSet_) {
                if (!rootObj)
                    continue;

                ObjectHeader *header = headerOf(rootObj);

                // Only young-gen objects need forwarding here
                if (inYoungGenSpace(header)) {
                    rootObj = forward(rootObj);
                }
            }

            // 3. Process old-to-young references (remembered set)
            for (ObjectHeader *oldHeader : rememberedSet_) {
                if (!oldHeader->isValid())
                    continue;

                rtdata::Object *oldObj = payloadOf<rtdata::Object>(oldHeader);

                // Walk and update refs in old-gen objects (layout is per Object / allocator
                // contract)
                oldObj->updateRefs(
                    [this](rtdata::Object *ref) -> rtdata::Object * {
                        if (!ref)
                            return nullptr;

                        ObjectHeader *refHeader = headerOf(ref);

                        // Young-gen targets must be forwarded (copied)
                        if (inYoungGenSpace(refHeader)) {
                            return forward(ref);
                        }

                        return ref;
                    },
                    nullptr);
            }

            // Remembered set is stale after the young-gen collection
            rememberedSet_.clear();

            // 4. Cheney scan over To (haven) space
            cheneyScavenge();

            // 5. Reset Birth and Cache
            birthSpace_.reset();
            cacheSpace_.reset();

            inGC_ = false;
        } catch (...) {
            inGC_ = false;
            throw;
        }
    }

    void majorGCUnlocked() {
        // 1. Mark phase: mark all reachable objects
        markPhase();

        // 2. Collect the young generation
        minorGCUnlocked();

        // 3. Sweep old generation (mark-sweep; no compaction here)
        sweepOldGen();

        // 4. Sweep large-object space
        sweepLargeObjects();
    }

    // ============================================================================
    // Allocation region tag
    // ============================================================================
    enum AllocRegion {
        YoungGen, // Young gen: newly created small objects
        ElderGen, // Old gen: long-lived objects after promotion
        LargeObj, // Large-object space: payloads above the threshold
    };

    // ============================================================================
    // Young generation — three-space copying collector
    // ============================================================================
    // Model: tri-color style bookkeeping with copying collection.
    // - birthSpace_: allocation nursery; first placement for small objects
    // - havenSpace_: current survivor To-space; holds survivors from the last cycle
    // - cacheSpace_: survivor From-space; swaps roles with havenSpace_
    //
    // Minor GC:
    //   1. Swap havenSpace_ ↔ cacheSpace_
    //   2. Copy live objects from birthSpace_ + cacheSpace_ (old To) into havenSpace_ (new To)
    //   3. Reset birthSpace_ and cacheSpace_
    // ============================================================================
    BumpPointerAllocator birthSpace_;
    BumpPointerAllocator havenSpace_;
    BumpPointerAllocator cacheSpace_;

    // ============================================================================
    // Old generation (Elder) — mark-sweep collector
    // ============================================================================
    // Holds promoted long-lived objects (age >= promotionAgeThreshold_).
    // FreeList manages fragmentation; good density, slower allocation than bump.
    // Major GC runs mark-sweep over this region.
    // ============================================================================
    FreeListAllocator elderGenSpace_;

    // ============================================================================
    // Large object space — separate region
    // ============================================================================
    // Allocates payloads larger than largeObjThreshold_ directly.
    // Avoids copying cost; managed in its own area; mark-sweep on major GC.
    // ============================================================================
    LargeObjectAllocator largeObjSpace_;

    // ============================================================================
    // GC tuning parameters
    // ============================================================================
    size_t promotionAgeThreshold_; // Promote after this many minor GC survivals
    size_t largeObjThreshold_;     // Objects larger than this go to large-object space
    float minorGCTriggerRatio_;    // Reserved: minor GC trigger ratio (e.g. when birth is full)
    float majorGCTriggerRatio_;    // Major GC trigger ratio (old-gen utilization)

    // ============================================================================
    // GC state and roots
    // ============================================================================
    bool inGC_ = false;                                // Reentrancy guard for nested GC
    std::vector<rtdata::Object *> *rootObjectSet_{};   // Roots: stack, globals, etc.
    std::unordered_set<ObjectHeader *> rememberedSet_; // Remembered set: old→young edges
    mutable std::mutex mutex_;

    bool inYoungGenSpace(ObjectHeader *header) const {
        return header->region_ == AllocRegion::YoungGen;
    }
    bool inElderGenSpace(ObjectHeader *header) const {
        return header->region_ == AllocRegion::ElderGen;
    }
    bool inLargeObjSpace(ObjectHeader *header) const {
        return header->region_ == AllocRegion::LargeObj;
    }

    rtdata::Object *forward(rtdata::Object *obj) {
        ObjectHeader *header = headerOf(obj);
        ASSERT(header->isValid(), "Invalid ObjectHeader encountered during forwarding");

        // Already forwarded: return the forwardee
        if (header->forwarded()) {
            return static_cast<rtdata::Object *>(header->forwardedAddr());
        }

        size_t objSize = header->objSize();

        // Bump survival age
        header->incAge();
        uint64_t age = header->age();

        void *newObj            = nullptr;
        ObjectHeader *newHeader = nullptr;

        // Promotion vs. copy to survivor To
        if (UNLIKELY(age >= promotionAgeThreshold_)) {
            // Promote to old generation
            newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
            if (!newObj) {
                // Old gen full: run full collection
                majorGCUnlocked();
                newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
                if (!newObj)
                    throw std::bad_alloc();
            }

            newHeader = headerOf(newObj);
            newHeader->setAge(age);
            newHeader->setRegion(AllocRegion::ElderGen);
        } else {
            // Copy into survivor To
            newObj = havenSpace_.alloc(objSize, alignof(slot_t));
            if (UNLIKELY(!newObj)) {
                // To space full: promote instead
                newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
                if (UNLIKELY(!newObj)) {
                    if (inGC_) {
                        // Already in GC; cannot recurse into another major pass here
                        throw std::bad_alloc();
                    }

                    // Run major GC from nested forward path
                    inGC_ = true;
                    try {
                        majorGCUnlocked();
                        newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
                        inGC_  = false;
                    } catch (...) {
                        inGC_ = false;
                        throw;
                    }

                    if (!newObj)
                        throw std::bad_alloc();
                }

                newHeader = headerOf(newObj);
                newHeader->setAge(age);
                newHeader->setRegion(AllocRegion::ElderGen);
            } else {
                newHeader = headerOf(newObj);
                newHeader->setAge(age);
                newHeader->setRegion(AllocRegion::YoungGen);
            }
        }

        // Copy payload bytes
        std::memcpy(newObj, (void *)obj, objSize);

        // Notify the moved object (fix interior pointers, etc.)
        rtdata::Object *gcObj = reinterpret_cast<rtdata::Object *>(newObj);
        gcObj->onMoved();

        // Install forwarding pointer in the old header
        header->forward(newObj);

        return static_cast<rtdata::Object *>(newObj);
    }

    // Cheney scan: BFS over copied objects in To space
    void cheneyScavenge() {
        std::byte *scan = havenSpace_.start(); // Scan cursor in To space
        std::byte *free = havenSpace_.top();   // Allocation frontier

        // BFS: scan catches up to free as copies append
        while (scan < free) {
            ObjectHeader *header = reinterpret_cast<ObjectHeader *>(scan);
            void *payload        = scan + sizeof(ObjectHeader);

            // Actual heap object
            rtdata::Object *ref = reinterpret_cast<rtdata::Object *>(payload);

            // Forward all reference fields (layout per Object / allocator contract)
            ref->updateRefs(
                [this](rtdata::Object *ref) -> rtdata::Object * {
                    if (!ref)
                        return nullptr;

                    ObjectHeader *refHeader = headerOf(ref);

                    // Old-gen targets are not moved by minor GC
                    if (!inYoungGenSpace(refHeader)) {
                        return ref;
                    }

                    // Copy or follow existing forward
                    return forward(ref);
                },
                nullptr);

            // Advance to next object in To space
            scan += header->size();
            // free may move when forward() copies more young objects
            free = havenSpace_.top();
        }
    }

    // Mark phase: depth-first mark all reachable objects.
    void markPhase() {
        // Clear all marks.
        clearMarks();

        // Start marking from the root set.
        for (rtdata::Object *root : *rootObjectSet_) {
            if (root) {
                markObject(root);
            }
        }
    }

    void clearMarks() {
        // Clear old-generation marks.
        elderGenSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });

        // Clear large-object-space marks.
        largeObjSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });

        if (rootObjectSet_) {
            for (rtdata::Object *root : *rootObjectSet_) {
                if (!root) {
                    continue;
                }
                ObjectHeader *header = headerOf(root);
                if (!inYoungGenSpace(header) && !inElderGenSpace(header) &&
                    !inLargeObjSpace(header)) {
                    header->unmark();
                }
            }
        }
    }

    void markObject(rtdata::Object *obj) {
        if (!obj)
            return;

        std::vector<rtdata::Object *> markStack;
        markStack.push_back(obj);

        while (!markStack.empty()) {
            rtdata::Object *current = markStack.back();
            markStack.pop_back();

            if (!current)
                continue;

            void *payload        = reinterpret_cast<void *>(current);
            ObjectHeader *header = headerOf(payload);

            // Skip if already marked.
            if (header->marked_)
                continue;

            // Mark the current object.
            header->mark();

            // Collect all referenced objects onto the stack (the type is
            // agreed between each Object and the allocator at creation time;
            // none is available here yet).
            current->updateRefs(
                [&markStack](rtdata::Object *ref) -> rtdata::Object * {
                    if (ref) {
                        markStack.push_back(ref);
                    }
                    return ref;
                },
                nullptr);
        }
    }

    // Sweep unmarked objects from the old generation.
    void sweepOldGen() {
        std::vector<ObjectHeader *> unreachable;

        elderGenSpace_.iterateAllocated([&unreachable](ObjectHeader *header) {
            if (!header->marked_) {
                unreachable.push_back(header);
            }
        });

        // Bulk free.
        elderGenSpace_.freeBulk(unreachable);
    }

    // Sweep unmarked objects from the large-object space.
    void sweepLargeObjects() {
        std::vector<ObjectHeader *> unreachable;

        largeObjSpace_.iterateAllocated([&unreachable](ObjectHeader *header) {
            if (!header->marked_) {
                unreachable.push_back(header);
            }
        });

        // Bulk free.
        largeObjSpace_.freeBulk(unreachable);
    }
};

} // namespace camel::core::mm
