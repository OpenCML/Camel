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
 * Updated: May. 24, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/bump_ptr.h"
#include "alloc/free_list.h"
#include "alloc/large_obj.h"
#include "camel/core/rtdata/base.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

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
namespace type   = camel::core::type;

namespace detail {
// Set only when diagnostic GC stress or a deferred safepoint collection can require real work.
// Hot schedulers use this as the single cheap predicate before taking the allocator mutex.
extern std::atomic_bool autoSpaceSafepointSlowPath;
} // namespace detail

FreeListAllocator &graphSpace();
BumpPointerAllocator &permSpace();

class GenerationalAllocatorWithGC : public IAllocator {
  public:
    using RefRelocator       = rtdata::Object::RefRelocator;
    using ExternalRootTracer = std::function<void(const RefRelocator &)>;

    enum class CollectionKind {
        None,
        Minor,
        Major,
        MinorAndMajor,
    };

    struct Config {
        size_t birthSize;
        size_t havenSize;
        size_t elderGenSize;
        size_t promotionAgeThreshold;
        size_t largeObjThreshold;
        float minorGCTriggerRatio;
        float majorGCTriggerRatio;
        bool enableYoungGenCopying;
    };

    struct DebugConfig {
        size_t stressEveryNAllocations  = 0;
        size_t stressEveryNSafepoints   = 0;
        CollectionKind stressCollection = CollectionKind::Major;
        bool verifyBeforeGC             = false;
        bool verifyAfterGC              = false;
        bool logMovements               = false;
    };

    struct RuntimeStats {
        uint64_t allocations                  = 0;
        uint64_t safepoints                   = 0;
        uint64_t deferredCollections          = 0;
        uint64_t requestedCollections         = 0;
        uint64_t allocationFailureCollections = 0;
        uint64_t writeBarriers                = 0;
        uint64_t minorCollections             = 0;
        uint64_t majorCollections             = 0;
        uint64_t movedObjects                 = 0;
        uint64_t promotedObjects              = 0;
        uint64_t freedElderObjects            = 0;
        uint64_t freedLargeObjects            = 0;
        uint64_t lastTracedRootReferenceCount = 0;
        size_t rememberedSetSize              = 0;
        size_t rootSourceCount                = 0;
    };

    struct HeapVerificationIssue {
        std::string message;
        std::string path;
        std::string owner;
        std::string slotType;
        std::string region;
        uintptr_t object = 0;
        uintptr_t target = 0;
    };

    explicit GenerationalAllocatorWithGC(const Config &config);

    void *alloc(size_t payloadSize, size_t align = alignof(slot_t)) override;

    void free(void *ptr) override;

    bool isObjectAddressStable(rtdata::Object *object) const;

    void setObjectRootSet(std::vector<rtdata::Object *> *rootSet);

    void registerExternalRootTracer(const void *owner, std::string name, ExternalRootTracer tracer);

    void unregisterExternalRootTracer(const void *owner);

    void configureDebug(DebugConfig config);

    DebugConfig debugConfig() const;

    RuntimeStats stats() const;

    std::vector<std::string> rootSourceDescriptions() const;

    std::vector<HeapVerificationIssue> verifyHeap();

    void verifyHeapOrThrow(std::string_view where);

    void safepoint(std::string_view reason = {});

    void writeBarrier(
        rtdata::Object *ownerObject, const type::Type *ownerType, slot_t storedSlot,
        const type::Type *storedType);

    void writeBarrier(
        rtdata::Object *ownerObject, const type::Type *ownerType, rtdata::Object *storedObject,
        const type::Type *storedType);

    void recordOldToYoungRef(
        rtdata::Object *oldObj, const type::Type *oldType, rtdata::Object *youngObj,
        const type::Type *youngType);

    bool debugRunRememberedSetSelfTest();

    // Minor GC: collect the young generation (Birth + From).
    void minorGC();

    // Debugger / profiler: expose sub-regions (e.g. memory visualization).
    const BumpPointerAllocator &birthSpace() const { return birthSpace_; }
    const BumpPointerAllocator &havenSpace() const { return havenSpace_; }
    const BumpPointerAllocator &cacheSpace() const { return cacheSpace_; }
    const FreeListAllocator &elderGenSpace() const { return elderGenSpace_; }
    const LargeObjectAllocator &largeObjSpace() const { return largeObjSpace_; }

    // Major GC: collect the entire heap.
    void majorGC();

    bool youngGenCopyingEnabled() const { return enableYoungGenCopying_; }

  private:
    void *allocUnlocked(size_t payloadSize, size_t align = alignof(slot_t));

    static CollectionKind combineCollectionKinds(CollectionKind lhs, CollectionKind rhs);

    void requestStressCollectionUnlocked(std::string_view reason);

    void requestCollectionAtSafepointUnlocked(CollectionKind kind, std::string_view reason);

    void collectAtSafepointUnlocked(CollectionKind kind, std::string_view reason);

    void collectNonMovingUnlocked(std::string_view reason);

    void minorGCUnlocked();

    void majorGCUnlocked();

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
    bool enableYoungGenCopying_;   // The runtime currently assumes stable raw object pointers.

    // ============================================================================
    // GC state and roots
    // ============================================================================
    bool inGC_ = false; // Reentrancy guard for nested GC
    // Untyped roots are reserved for graph carriers whose updateRefs implementation owns its
    // layout. Runtime frames and temporary values must register typed external tracers instead.
    std::vector<rtdata::Object *> *rootObjectSet_{};
    struct ExternalRootTracerEntry {
        const void *owner = nullptr;
        std::string name;
        ExternalRootTracer tracer;
    };
    std::vector<ExternalRootTracerEntry> externalRootTracers_;
    struct RememberedSetEntry {
        rtdata::Object *object = nullptr;
        const type::Type *type = nullptr;
    };
    // Old/large auto-space objects that currently contain at least one young reference. The typed
    // layout is required because composite objects do not carry their own runtime type metadata.
    std::unordered_map<ObjectHeader *, RememberedSetEntry> rememberedSet_;
    std::unordered_map<ObjectHeader *, const type::Type *> objectTypes_;
    DebugConfig debugConfig_{};
    RuntimeStats stats_{};
    CollectionKind pendingSafepointCollection_ = CollectionKind::None;
    mutable std::mutex mutex_;

    struct TraceEntry {
        rtdata::Object *object = nullptr;
        const type::Type *type = nullptr;
    };

    std::vector<TraceEntry> relocationScan_;
    size_t relocationScanIndex_ = 0;

    ObjectHeader *autoHeaderForPayload(rtdata::Object *object) const;

    bool inYoungGenSpace(ObjectHeader *header) const;
    bool inElderGenSpace(ObjectHeader *header) const;
    bool inLargeObjSpace(ObjectHeader *header) const;

    static bool isPreciseGCType(const type::Type *objectType);

    void recordObjectTypeUnlocked(rtdata::Object *object, const type::Type *objectType);

    const type::Type *knownObjectTypeUnlocked(ObjectHeader *header) const;

    struct RememberedEdge {
        ObjectHeader *ownerHeader   = nullptr;
        rtdata::Object *owner       = nullptr;
        const type::Type *ownerType = nullptr;
        rtdata::Object *target      = nullptr;
        const type::Type *slotType  = nullptr;
        rtdata::RefTraceInfo info;
    };

    std::vector<RememberedEdge>
    collectYoungReferenceEdges(rtdata::Object *object, const type::Type *objectType) const;

    void rememberOldObjectIfYoungRefsUnlocked(rtdata::Object *object, const type::Type *objectType);

    void finalizeUnforwardedObjectsIn(BumpPointerAllocator &allocator);

    void finalizeObjects(const std::vector<ObjectHeader *> &objects);

    rtdata::Object *forward(rtdata::Object *obj, const type::Type *objType);

    rtdata::Object *forwardYoungRef(rtdata::Object *ref, const type::Type *refType);

    void scanRelocatedObjects();

    size_t rootSourceCountUnlocked() const;

    static std::string formatVerificationIssue(const HeapVerificationIssue &issue);

    static HeapVerificationIssue makeIssue(
        std::string message, std::string path = {}, std::string owner = {},
        std::string slotType = {}, std::string region = {}, uintptr_t object = 0,
        uintptr_t target = 0);

    void throwIfVerificationFailed(
        const std::vector<HeapVerificationIssue> &issues, std::string_view where) const;

    bool autoRegionContainsPayload(
        const BumpPointerAllocator &allocator, rtdata::Object *object, std::string_view regionName,
        AllocRegion expectedRegion, std::vector<HeapVerificationIssue> &issues,
        std::string_view path) const;

    bool autoRegionContainsPayload(
        const FreeListAllocator &allocator, rtdata::Object *object, std::string_view regionName,
        AllocRegion expectedRegion, std::vector<HeapVerificationIssue> &issues,
        std::string_view path) const;

    bool autoRegionContainsPayload(
        const LargeObjectAllocator &allocator, rtdata::Object *object,
        std::vector<HeapVerificationIssue> &issues, std::string_view path) const;

    bool knownObjectPointer(
        rtdata::Object *object, std::vector<HeapVerificationIssue> &issues,
        std::string_view path) const;

    void validateAllocatedHeaders(
        std::vector<HeapVerificationIssue> &issues, const BumpPointerAllocator &allocator,
        std::string_view regionName, AllocRegion expectedRegion) const;

    void validateAllocatedHeaders(
        std::vector<HeapVerificationIssue> &issues, const FreeListAllocator &allocator,
        std::string_view regionName, AllocRegion expectedRegion) const;

    void validateAllocatedHeaders(
        std::vector<HeapVerificationIssue> &issues, const LargeObjectAllocator &allocator) const;

    std::vector<HeapVerificationIssue> verifyHeapUnlocked();

    void verifyRememberedSetUnlocked(std::vector<HeapVerificationIssue> &issues);

    // Mark phase: depth-first mark all reachable objects.
    void markPhase();

    void clearMarks();

    void markObject(rtdata::Object *obj, const type::Type *objType);

    // Sweep unmarked objects from the old generation.
    void sweepOldGen();

    // Sweep unmarked objects from the large-object space.
    void sweepLargeObjects();
};

GenerationalAllocatorWithGC &autoSpace();

inline bool autoSpaceSafepointSlowPathEnabled() noexcept {
    return detail::autoSpaceSafepointSlowPath.load(std::memory_order_acquire);
}

inline void safepoint(std::string_view reason = {}) {
    if (autoSpaceSafepointSlowPathEnabled()) {
        autoSpace().safepoint(reason);
    }
}

} // namespace camel::core::mm
