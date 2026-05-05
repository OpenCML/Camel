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
 * Created: May. 05, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/mm/gen.h"

#include "camel/utils/assert.h"
#include "camel/utils/brpred.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <mutex>
#include <new>
#include <string>
#include <utility>

// Core allocation, collection, relocation, and mark/sweep implementation for the
// generational GC. Debug configuration and heap verification live in gc_debug.cpp.

namespace camel::core::mm {

GenerationalAllocatorWithGC::GenerationalAllocatorWithGC(const Config &config)
    : birthSpace_(config.birthSize, "auto.birth"), havenSpace_(config.havenSize, "auto.haven"),
      cacheSpace_(config.havenSize, "auto.cache"),
      elderGenSpace_(config.elderGenSize, "auto.elder"), largeObjSpace_("auto.large"),
      promotionAgeThreshold_(config.promotionAgeThreshold),
      largeObjThreshold_(config.largeObjThreshold),
      minorGCTriggerRatio_(config.minorGCTriggerRatio),
      majorGCTriggerRatio_(config.majorGCTriggerRatio),
      enableYoungGenCopying_(config.enableYoungGenCopying) {}

void *GenerationalAllocatorWithGC::alloc(size_t payloadSize, size_t align) {
    std::lock_guard<std::mutex> lock(mutex_);
    return allocUnlocked(payloadSize, align);
}

void GenerationalAllocatorWithGC::free(void *ptr) {
    (void)ptr;
    ASSERT(false, "GenerationalAllocatorWithGC does not support manual free");
}

void GenerationalAllocatorWithGC::setObjectRootSet(std::vector<rtdata::Object *> *rootSet) {
    std::lock_guard<std::mutex> lock(mutex_);
    rootObjectSet_ = rootSet;
}

void GenerationalAllocatorWithGC::registerExternalRootTracer(
    const void *owner, std::string name, ExternalRootTracer tracer) {
    std::lock_guard<std::mutex> lock(mutex_);
    ASSERT(owner != nullptr, "External GC root tracer owner cannot be null.");
    ASSERT(!name.empty(), "External GC root tracer name cannot be empty.");
    ASSERT(static_cast<bool>(tracer), "External GC root tracer cannot be empty.");
    auto it = std::find_if(
        externalRootTracers_.begin(),
        externalRootTracers_.end(),
        [owner](const auto &entry) { return entry.owner == owner; });
    if (it != externalRootTracers_.end()) {
        it->name   = std::move(name);
        it->tracer = std::move(tracer);
        return;
    }
    externalRootTracers_.push_back(
        ExternalRootTracerEntry{owner, std::move(name), std::move(tracer)});
}

void GenerationalAllocatorWithGC::unregisterExternalRootTracer(const void *owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(externalRootTracers_, [owner](const auto &entry) {
        return entry.owner == owner;
    });
}

void GenerationalAllocatorWithGC::safepoint(std::string_view reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.safepoints;
    if (debugConfig_.stressEveryNSafepoints != 0 &&
        stats_.safepoints % debugConfig_.stressEveryNSafepoints == 0) {
        requestStressCollectionUnlocked("safepoint interval");
    }

    CollectionKind requested = pendingStressCollection_;
    pendingStressCollection_ = CollectionKind::None;
    collectUnlocked(requested, reason.empty() ? "safepoint" : reason);
}

void GenerationalAllocatorWithGC::recordOldToYoungRef(void *oldObj, void *youngObj) {
    (void)youngObj;
    if (!enableYoungGenCopying_) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    ObjectHeader *header = headerOf(oldObj);
    rememberedSet_.insert(header);
}

void GenerationalAllocatorWithGC::minorGC() {
    std::lock_guard<std::mutex> lock(mutex_);
    collectUnlocked(CollectionKind::Minor, "manual minor GC");
}

void GenerationalAllocatorWithGC::majorGC() {
    std::lock_guard<std::mutex> lock(mutex_);
    collectUnlocked(CollectionKind::Major, "manual major GC");
}

void *GenerationalAllocatorWithGC::allocUnlocked(size_t payloadSize, size_t align) {
    ASSERT(align == alignof(slot_t), "Alignment other than 8 bytes is not supported");
    ++stats_.allocations;
    if (debugConfig_.stressEveryNAllocations != 0 &&
        stats_.allocations % debugConfig_.stressEveryNAllocations == 0) {
        requestStressCollectionUnlocked("allocation interval");
    }

    // Large objects use the large-object allocator directly.
    if (UNLIKELY(payloadSize > largeObjThreshold_)) {
        void *ptr = largeObjSpace_.alloc(payloadSize, align);
        if (UNLIKELY(!ptr)) {
            collectUnlocked(CollectionKind::Major, "large object allocation failure");
            ptr = largeObjSpace_.alloc(payloadSize, align);
            if (!ptr)
                throw std::bad_alloc();
        }
        auto *header = headerOf(ptr);
        header->setRegion(AllocRegion::LargeObj);
        return ptr;
    }

    if (!enableYoungGenCopying_) {
        void *ptr = elderGenSpace_.alloc(payloadSize, align);
        if (UNLIKELY(!ptr)) {
            collectUnlocked(CollectionKind::Major, "elder allocation failure");
            ptr = elderGenSpace_.alloc(payloadSize, align);
            if (!ptr)
                throw std::bad_alloc();
        }

        auto *header = headerOf(ptr);
        header->setRegion(AllocRegion::ElderGen);
        return ptr;
    }

    // Try allocating in the birth space first.
    void *ptr = birthSpace_.alloc(payloadSize, align);
    if (UNLIKELY(!ptr)) {
        collectUnlocked(CollectionKind::Minor, "birth allocation failure");
        ptr = birthSpace_.alloc(payloadSize, align);
        if (UNLIKELY(!ptr)) {
            collectUnlocked(CollectionKind::Major, "birth allocation failure after minor GC");
            ptr = birthSpace_.alloc(payloadSize, align);
            if (!ptr)
                throw std::bad_alloc();
        }
    }

    auto *header = headerOf(ptr);
    header->setRegion(AllocRegion::YoungGen);
    return ptr;
}

GenerationalAllocatorWithGC::CollectionKind
GenerationalAllocatorWithGC::combineCollectionKinds(CollectionKind lhs, CollectionKind rhs) {
    if (lhs == CollectionKind::None) {
        return rhs;
    }
    if (rhs == CollectionKind::None || lhs == rhs) {
        return lhs;
    }
    return CollectionKind::MinorAndMajor;
}

void GenerationalAllocatorWithGC::requestStressCollectionUnlocked(std::string_view reason) {
    (void)reason;
    pendingStressCollection_ =
        combineCollectionKinds(pendingStressCollection_, debugConfig_.stressCollection);
}

void GenerationalAllocatorWithGC::collectUnlocked(CollectionKind kind, std::string_view reason) {
    if (kind == CollectionKind::None) {
        return;
    }
    ++stats_.requestedCollections;

    if (debugConfig_.verifyBeforeGC) {
        throwIfVerificationFailed(verifyHeapUnlocked(), std::format("before {}", reason));
    }

    switch (kind) {
    case CollectionKind::Minor:
        minorGCUnlocked();
        break;
    case CollectionKind::Major:
        majorGCUnlocked();
        break;
    case CollectionKind::MinorAndMajor:
        minorGCUnlocked();
        majorGCUnlocked();
        break;
    case CollectionKind::None:
        break;
    }

    if (debugConfig_.verifyAfterGC) {
        throwIfVerificationFailed(verifyHeapUnlocked(), std::format("after {}", reason));
    }
}

void GenerationalAllocatorWithGC::minorGCUnlocked() {
    if (!enableYoungGenCopying_) {
        return;
    }
    if (inGC_)
        return; // Reentrancy guard
    inGC_ = true;
    ++stats_.minorCollections;

    try {
        relocationScan_.clear();
        relocationScanIndex_ = 0;

        // 1. Swap Cache and Haven
        cacheSpace_.swap(havenSpace_);
        havenSpace_.reset(); // Clear the new Haven (To) space

        auto relocateYoung =
            [this](rtdata::Object *ref, const type::Type *refType, const rtdata::RefTraceInfo &)
            -> rtdata::Object * { return forwardYoungRef(ref, refType); };

        // 2. Forward young-gen objects referenced from roots. The root set currently stores
        // runtime GCGraph carriers, which live in graphSpace rather than autoSpace; tracing the
        // root object's fields is therefore required to update static data roots.
        if (rootObjectSet_) {
            for (rtdata::Object *&rootObj : *rootObjectSet_) {
                if (!rootObj) {
                    continue;
                }

                rootObj = forwardYoungRef(rootObj, nullptr);
                rootObj->updateRefs(relocateYoung, nullptr);
            }
        }
        for (const auto &entry : externalRootTracers_) {
            entry.tracer(relocateYoung);
        }

        // 3. Process old-to-young references (remembered set)
        for (ObjectHeader *oldHeader : rememberedSet_) {
            if (!oldHeader->isValid())
                continue;

            rtdata::Object *oldObj = payloadOf<rtdata::Object>(oldHeader);

            // Walk and update refs in old-gen objects (layout is per Object / allocator contract).
            oldObj->updateRefs(relocateYoung, nullptr);
        }

        // Remembered set is stale after the young-gen collection.
        rememberedSet_.clear();

        // 4. Scan copied/promoted objects with their static layouts.
        scanRelocatedObjects();

        // 5. Reset Birth and Cache
        birthSpace_.reset();
        cacheSpace_.reset();

        relocationScan_.clear();
        relocationScanIndex_ = 0;
        inGC_                = false;
    } catch (...) {
        relocationScan_.clear();
        relocationScanIndex_ = 0;
        inGC_                = false;
        throw;
    }
}

void GenerationalAllocatorWithGC::majorGCUnlocked() {
    ++stats_.majorCollections;
    // 1. Mark phase: mark all reachable objects
    markPhase();

    // 2. Collect the young generation
    minorGCUnlocked();

    // 3. Sweep old generation (mark-sweep; no compaction here)
    sweepOldGen();

    // 4. Sweep large-object space
    sweepLargeObjects();
}

bool GenerationalAllocatorWithGC::inYoungGenSpace(ObjectHeader *header) const {
    return header && header->region_ == AllocRegion::YoungGen &&
           (birthSpace_.contains(header) || havenSpace_.contains(header) ||
            cacheSpace_.contains(header));
}

bool GenerationalAllocatorWithGC::inElderGenSpace(ObjectHeader *header) const {
    return header && header->region_ == AllocRegion::ElderGen && elderGenSpace_.contains(header);
}

bool GenerationalAllocatorWithGC::inLargeObjSpace(ObjectHeader *header) const {
    return header && header->region_ == AllocRegion::LargeObj &&
           largeObjSpace_.contains(payloadOf<rtdata::Object>(header));
}

rtdata::Object *
GenerationalAllocatorWithGC::forward(rtdata::Object *obj, const type::Type *objType) {
    ObjectHeader *header = headerOf(obj);
    ASSERT(header->isValid(), "Invalid ObjectHeader encountered during forwarding");

    // Already forwarded: return the forwardee.
    if (header->forwarded()) {
        return static_cast<rtdata::Object *>(header->forwardedAddr());
    }

    size_t objSize = header->objSize();

    // Bump survival age.
    header->incAge();
    uint64_t age = header->age();

    void *newObj            = nullptr;
    ObjectHeader *newHeader = nullptr;

    bool promoted = false;
    if (UNLIKELY(age >= promotionAgeThreshold_)) {
        // Promote to old generation.
        newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
        if (!newObj) {
            // Old gen full: run full collection.
            majorGCUnlocked();
            newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
            if (!newObj)
                throw std::bad_alloc();
        }

        newHeader = headerOf(newObj);
        newHeader->setAge(age);
        newHeader->setRegion(AllocRegion::ElderGen);
        promoted = true;
    } else {
        // Copy into survivor To.
        newObj = havenSpace_.alloc(objSize, alignof(slot_t));
        if (UNLIKELY(!newObj)) {
            // To space full: promote instead.
            newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
            if (UNLIKELY(!newObj)) {
                if (inGC_) {
                    // Already in GC; cannot recurse into another major pass here.
                    throw std::bad_alloc();
                }

                // Run major GC from nested forward path.
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
            promoted = true;
        } else {
            newHeader = headerOf(newObj);
            newHeader->setAge(age);
            newHeader->setRegion(AllocRegion::YoungGen);
        }
    }

    // Copy payload bytes.
    std::memcpy(newObj, (void *)obj, objSize);

    // Notify the moved object (fix interior pointers, etc.).
    rtdata::Object *gcObj = reinterpret_cast<rtdata::Object *>(newObj);
    gcObj->onMoved();
    relocationScan_.push_back(TraceEntry{gcObj, objType});
    ++stats_.movedObjects;
    if (promoted) {
        ++stats_.promotedObjects;
    }
    if (debugConfig_.logMovements) {
        CAMEL_LOG_INFO_S(
            "GC",
            "move object old={} new={} type={} region={}",
            formatAddress(obj, true),
            formatAddress(newObj, true),
            objType ? objType->toString() : std::string{"<unknown>"},
            promoted ? "elder" : "young");
    }

    // Install forwarding pointer in the old header.
    header->forward(newObj);

    return static_cast<rtdata::Object *>(newObj);
}

rtdata::Object *
GenerationalAllocatorWithGC::forwardYoungRef(rtdata::Object *ref, const type::Type *refType) {
    if (!ref) {
        return nullptr;
    }
    ObjectHeader *refHeader = headerOf(ref);
    if (inYoungGenSpace(refHeader)) {
        return forward(ref, refType);
    }
    return ref;
}

void GenerationalAllocatorWithGC::scanRelocatedObjects() {
    while (relocationScanIndex_ < relocationScan_.size()) {
        TraceEntry entry = relocationScan_[relocationScanIndex_++];
        if (!entry.object || !entry.type) {
            continue;
        }
        entry.object->updateRefs(
            [this](rtdata::Object *ref, const type::Type *refType, const rtdata::RefTraceInfo &)
                -> rtdata::Object * { return forwardYoungRef(ref, refType); },
            entry.type);
    }
}

void GenerationalAllocatorWithGC::markPhase() {
    clearMarks();

    if (rootObjectSet_) {
        for (rtdata::Object *root : *rootObjectSet_) {
            if (root) {
                markObject(root, nullptr);
            }
        }
    }
    for (const auto &entry : externalRootTracers_) {
        entry.tracer(
            [this](rtdata::Object *ref, const type::Type *refType, const rtdata::RefTraceInfo &)
                -> rtdata::Object * {
                if (ref) {
                    markObject(ref, refType);
                }
                return ref;
            });
    }
}

void GenerationalAllocatorWithGC::clearMarks() {
    // Young generation marks matter when the copying collector is enabled. Clearing them keeps
    // a later major collection from skipping an already-marked survivor and missing its edges.
    birthSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });
    havenSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });
    cacheSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });

    elderGenSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });
    largeObjSpace_.iterateAllocated([](ObjectHeader *header) { header->unmark(); });

    if (rootObjectSet_) {
        for (rtdata::Object *root : *rootObjectSet_) {
            if (!root) {
                continue;
            }
            ObjectHeader *header = headerOf(root);
            if (!inYoungGenSpace(header) && !inElderGenSpace(header) && !inLargeObjSpace(header)) {
                header->unmark();
            }
        }
    }
}

void GenerationalAllocatorWithGC::markObject(rtdata::Object *obj, const type::Type *objType) {
    if (!obj)
        return;

    std::vector<TraceEntry> markStack;
    markStack.push_back(TraceEntry{obj, objType});

    while (!markStack.empty()) {
        TraceEntry entry        = markStack.back();
        rtdata::Object *current = entry.object;
        markStack.pop_back();

        if (!current)
            continue;

        void *payload        = reinterpret_cast<void *>(current);
        ObjectHeader *header = headerOf(payload);

        if (header->marked_)
            continue;

        header->mark();

        // The parent layout supplies refType wherever static slot metadata is available.
        current->updateRefs(
            [&markStack](
                rtdata::Object *ref,
                const type::Type *refType,
                const rtdata::RefTraceInfo &) -> rtdata::Object * {
                if (ref) {
                    markStack.push_back(TraceEntry{ref, refType});
                }
                return ref;
            },
            entry.type);
    }
}

void GenerationalAllocatorWithGC::sweepOldGen() {
    std::vector<ObjectHeader *> unreachable;

    elderGenSpace_.iterateAllocated([&unreachable](ObjectHeader *header) {
        if (!header->marked_) {
            unreachable.push_back(header);
        }
    });

    stats_.freedElderObjects += unreachable.size();
    elderGenSpace_.freeBulk(unreachable);
}

void GenerationalAllocatorWithGC::sweepLargeObjects() {
    std::vector<ObjectHeader *> unreachable;

    largeObjSpace_.iterateAllocated([&unreachable](ObjectHeader *header) {
        if (!header->marked_) {
            unreachable.push_back(header);
        }
    });

    stats_.freedLargeObjects += unreachable.size();
    largeObjSpace_.freeBulk(unreachable);
}

} // namespace camel::core::mm
