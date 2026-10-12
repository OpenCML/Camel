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
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "alloc/bump_ptr.h"
#include "alloc/free_list.h"
#include "alloc/growable.h"
#include "alloc/large_obj.h"
#include "camel/core/rtdata/base.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
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
// Activations of JIT-compiled code now on a native stack. Compiled code has no stack maps, so
// no collection may run while one exists: a safepoint reached with this above zero is refused
// (GenerationalAllocatorWithGC::safepoint).
extern std::atomic<uint32_t> nativeCodeDepth;
} // namespace detail

GrowableFreeListAllocator &graphSpace();
GrowableBumpAllocator &permSpace();

class RootHandle;

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
        // Minimum bytes of large-object allocation between collections. Large
        // objects bypass the generational spaces, so without this budget they
        // would only be reclaimed when small-object pressure forces a major GC.
        size_t largeObjCollectionBudget;
        // Growth allowed between two collections, in percent of the large-object volume the
        // last sweep left: the budget is the larger of largeObjCollectionBudget and
        // live * percent / 100. 100 lets the space double before it is collected again.
        size_t largeObjGrowthPercent;
        // Share of the birth space that, once in use, makes the allocator ask for a minor
        // collection at the next safepoint (moving collector only). Asking before the space
        // is full keeps what is allocated up to that safepoint out of the elder space.
        float minorGCTriggerRatio;
        // Old-space bytes (elder + large) the heap may hold; 0: no limit but the host's
        // memory (CAMEL_GC_HEAP_LIMIT_MB). See allocation and safepoint for how it is enforced.
        size_t heapLimitBytes;
        bool enableYoungGenCopying;
    };

    struct DebugConfig {
        size_t stressEveryNAllocations  = 0;
        size_t stressEveryNSafepoints   = 0;
        CollectionKind stressCollection = CollectionKind::Major;
        bool verifyBeforeGC             = false;
        bool verifyAfterGC              = false;
        bool logMovements               = false;
        // Safepoint poisoning (CAMEL_GC_POISON): every safepoint evacuates the whole young
        // generation and overwrites the addresses the objects had with a poison word, so a
        // pointer that C++ code kept across the safepoint reads poison instead of a value
        // that happens to be still right. Requires young copying. See poison.h.
        bool poisonAtSafepoints = false;
    };

    struct RuntimeStats {
        uint64_t allocations                  = 0;
        uint64_t safepoints                   = 0;
        uint64_t inhibitedSafepoints          = 0; // Reached inside a NoCollectionScope
        uint64_t deferredCollections          = 0;
        uint64_t requestedCollections         = 0;
        uint64_t elderOverflowAllocations     = 0;
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
        // Pacing (reported with CAMEL_GC_STATS). Requests are counted once per collection they
        // lead to, by the cause that asked first; deferredCollections counts every repeat.
        uint64_t largeBudgetRequests    = 0;
        uint64_t elderExhaustedRequests = 0;
        uint64_t otherRequests          = 0; // Stress settings, explicit calls, a full birth space
        // Payload bytes handed out, per space. Small objects that found no room in the elder
        // space are counted in largeBytesAllocated (and in elderOverflowBytes).
        uint64_t youngBytesAllocated = 0;
        uint64_t elderBytesAllocated = 0;
        uint64_t largeBytesAllocated = 0;
        uint64_t elderOverflowBytes  = 0;
        // Large-object space: what the last sweep left, the budget in force, and the most the
        // space held (what the last sweep left plus what was allocated since).
        size_t largeLiveBytes     = 0;
        size_t largeBudgetBytes   = 0;
        size_t largeHighWaterMark = 0;
        size_t largeGrowthPercent = 0;
        // Elder space: what the last sweep left, the budget in force, what is reserved.
        size_t elderLiveBytes     = 0;
        size_t elderBudgetBytes   = 0;
        size_t elderReservedBytes = 0;
        size_t elderSegments      = 0;
        // Time the program was stopped for collections (verification excluded).
        uint64_t minorPauseNs    = 0;
        uint64_t minorPauseMaxNs = 0;
        uint64_t majorPauseNs    = 0;
        uint64_t majorPauseMaxNs = 0;
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

    void noteFinalizable(void *payload) override;

    bool isObjectAddressStable(rtdata::Object *object) const;

    void setObjectRootSet(std::vector<rtdata::Object *> *rootSet);

    void registerExternalRootTracer(const void *owner, std::string name, ExternalRootTracer tracer);

    void unregisterExternalRootTracer(const void *owner);

    // RootHandle registration (root_handle.h): constant time, no allocation.
    void linkRootHandle(RootHandle *handle);
    void unlinkRootHandle(RootHandle *handle);

    void configureDebug(DebugConfig config);

    DebugConfig debugConfig() const;

    RuntimeStats stats() const;

    // 0: silent; 1: reportStats is meant to be called at exit; 2: also one line on stderr per
    // major collection (CAMEL_GC_STATS).
    void setStatsLevel(int level);

    // One line: collections, requests by cause, bytes allocated per space, the large-object
    // space after the last sweep, its high-water mark and the budget in force.
    void reportStats(std::ostream &os) const;

    std::vector<std::string> rootSourceDescriptions() const;

    std::vector<HeapVerificationIssue> verifyHeap();

    void verifyHeapOrThrow(std::string_view where);

    // The only place a collection may start (together with minorGC/majorGC below, which are
    // safepoints of their caller). Allocation never collects and never moves an object: it
    // can only ask for a collection at the next safepoint. collectAtSafepointUnlocked checks
    // this (atSafepoint_), in release builds too.
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

    // White-box check of safepoint poisoning (std::gc::poison): an address kept across a
    // safepoint reads the poison word, a RootHandle follows the object. Needs CAMEL_GC_POISON.
    bool debugRunPoisonSelfTest();

    // Minor GC: collect the young generation (Birth + From).
    void minorGC();

    // Debugger / profiler: expose sub-regions (e.g. memory visualization).
    const BumpPointerAllocator &birthSpace() const { return birthSpace_; }
    const BumpPointerAllocator &havenSpace() const { return havenSpace_; }
    const BumpPointerAllocator &cacheSpace() const { return cacheSpace_; }
    const GrowableFreeListAllocator &elderGenSpace() const { return elderGenSpace_; }
    const LargeObjectAllocator &largeObjSpace() const { return largeObjSpace_; }

    // Major GC: collect the entire heap.
    void majorGC();

    bool youngGenCopyingEnabled() const { return enableYoungGenCopying_; }

    // See NoCollectionScope.
    void inhibitCollections() noexcept { collectionInhibit_.fetch_add(1); }
    void allowCollections() noexcept { collectionInhibit_.fetch_sub(1); }

  private:
    void *allocUnlocked(size_t payloadSize, size_t align = alignof(slot_t));

    void *allocLargeUnlocked(size_t payloadSize, size_t align);

    // Small-object allocation in the elder space, which grows by segments. It never
    // collects: collection happens at safepoints only (see the definition).
    void *allocElderUnlocked(size_t payloadSize, size_t align);

    // Old-space bytes in use: elder blocks plus large objects.
    size_t oldBytesHeldUnlocked() const;

    // Enforces heapLimitBytes for an old-space allocation of `requestBytes` (see definition).
    void checkHeapLimitUnlocked(size_t requestBytes);

    static CollectionKind combineCollectionKinds(CollectionKind lhs, CollectionKind rhs);

    void requestStressCollectionUnlocked(std::string_view reason);

    // `reason` must be a string literal: it is kept until the collection runs.
    void requestCollectionAtSafepointUnlocked(CollectionKind kind, std::string_view reason);

    void collectAtSafepointUnlocked(CollectionKind kind, std::string_view reason);

    void publishSafepointSlowPathUnlocked();

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
    GrowableFreeListAllocator elderGenSpace_;

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
    size_t promotionAgeThreshold_;    // Promote after this many minor GC survivals
    size_t largeObjThreshold_;        // Objects larger than this go to large-object space
    size_t largeObjMinBudget_;        // Floor of the large-object allocation budget
    size_t largeObjGrowthPercent_;    // Budget above the floor: this share of the live volume
    size_t largeObjBudget_;           // Large-object bytes allowed before requesting a major GC
    size_t largeBytesSinceSweep_ = 0; // Large-object bytes allocated since the last sweep
    size_t largeLiveBytes_       = 0; // Large-object bytes the last sweep left
    // Elder-space pacing, the same rule as for large objects: a major collection is asked for
    // once the space took elderBudget_ bytes since the last sweep, and after a sweep the
    // budget is the larger of the floor (the first segment) and the growth percentage of what
    // the sweep left alive.
    size_t elderMinBudget_;
    size_t elderBudget_;
    size_t elderBytesSinceSweep_ = 0; // Bytes the elder space took since the last sweep
    size_t elderLiveBytes_       = 0; // Elder bytes the last sweep left
    size_t minorTriggerBytes_;        // Birth-space occupancy that asks for a minor collection
    size_t heapLimitBytes_;           // 0: none
    bool heapLimitExceeded_ = false;  // Set by an allocation; resolved at the next safepoint
    size_t heapLimitRequest_ = 0;     // ...the allocation that crossed the limit
    bool enableYoungGenCopying_;      // Young objects are copied (and so move) when set.

    // ============================================================================
    // GC state and roots
    // ============================================================================
    bool inGC_ = false; // Reentrancy guard for nested GC
    // Set while safepoint(), minorGC() or majorGC() runs. A collection that starts anywhere
    // else (an allocation, a barrier) would move or free objects under C++ code that holds
    // them; it is refused with a logic error instead.
    bool atSafepoint_ = false;
    // Open NoCollectionScopes. While it is not zero a safepoint collects nothing; what was
    // asked for stays pending.
    std::atomic<uint32_t> collectionInhibit_{0};
    // Untyped roots are reserved for graph carriers whose updateRefs implementation owns its
    // layout. Runtime frames and temporary values must register typed external tracers instead.
    std::vector<rtdata::Object *> *rootObjectSet_{};
    struct ExternalRootTracerEntry {
        const void *owner = nullptr;
        std::string name;
        ExternalRootTracer tracer;
    };
    std::vector<ExternalRootTracerEntry> externalRootTracers_;
    RootHandle *rootHandles_ = nullptr; // Intrusive list, newest first
    size_t rootHandleCount_  = 0;
    // Calls every registered root source: the external tracers and the root handles. `visit`
    // receives the name of the source and a function that traces it with a relocator.
    void forEachRootSourceUnlocked(
        const std::function<
            void(std::string_view name, const std::function<void(const RefRelocator &)> &trace)>
            &visit);
    struct RememberedSetEntry {
        rtdata::Object *object = nullptr;
        const type::Type *type = nullptr;
    };
    // Old/large auto-space objects that currently contain at least one young reference. The typed
    // layout is required because composite objects do not carry their own runtime type metadata.
    std::unordered_map<ObjectHeader *, RememberedSetEntry> rememberedSet_;
    std::unordered_map<ObjectHeader *, const type::Type *> objectTypes_;
    bool recordLayouts_ = false; // objectTypes_ is being filled (see recordObjectTypeUnlocked)
    // Moving collector: objects that were born old (a large object, or a small one allocated
    // while the birth space was full) since the last minor collection. Their creator fills
    // them through plain stores, with no write barrier, so they may point at young objects
    // without being remembered. The next minor collection scans each one it reaches through a
    // typed reference (from a root, a young object, a remembered owner or another fresh
    // object), which is every way such an object can be reached: a store of it into an older
    // object goes through the barrier, which remembers the owner.
    std::unordered_set<ObjectHeader *> freshOld_;
    std::atomic<bool> anyFreshOld_{false}; // freshOld_ is not empty (read by the barrier)
    // Young objects with a finalizer (noteFinalizable). A minor collection frees dead young
    // objects by resetting their spaces, without visiting them: these are the ones it must
    // visit. Old objects are visited by the sweep.
    std::vector<rtdata::Object *> youngFinalizables_;
    // Graph carriers whose static area this minor collection has traced. Program graphs are
    // roots; a graph created while the program runs is reached through a Function value.
    std::unordered_set<rtdata::Object *> minorTracedGraphs_;
    std::vector<rtdata::Object *> minorGraphScan_;
    DebugConfig debugConfig_{};
    RuntimeStats stats_{};
    CollectionKind pendingSafepointCollection_ = CollectionKind::None;
    // Why the pending collection was first asked for. Always a string literal.
    std::string_view pendingCause_{};
    int statsLevel_ = 0;
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

    // Whether `object` holds a reference a minor collection must follow (collectYoungReferenceEdges
    // without the list).
    bool hasYoungRefsUnlocked(rtdata::Object *object, const type::Type *objectType) const;

    // Runs the finalizers of the young objects this minor collection did not copy.
    void finalizeDeadYoungUnlocked();

    void finalizeObjects(const std::vector<ObjectHeader *> &objects);

    rtdata::Object *forward(rtdata::Object *obj, const type::Type *objType);

    rtdata::Object *forwardYoungRef(rtdata::Object *ref, const type::Type *refType);

    // Whether a minor collection must look at `header`'s object to keep it or what it points
    // at alive and up to date: a young object, or one born old since the last one.
    bool youngOrFreshUnlocked(ObjectHeader *header) const;

    void scanRelocatedObjects();

    // The relocation callback of a minor collection: forwards young objects, queues objects
    // born old and graph carriers for a scan of their own.
    rtdata::Object *relocateForMinor(
        rtdata::Object *ref, const type::Type *refType, const rtdata::RefTraceInfo &info);

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
        const GrowableFreeListAllocator &allocator, rtdata::Object *object,
        std::string_view regionName,
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
        std::vector<HeapVerificationIssue> &issues, const GrowableFreeListAllocator &allocator,
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

/// While one is alive, safepoints do not collect: requests stay pending and are served by the
/// first safepoint after the last scope has closed.
///
/// For native code that holds raw object pointers and, in the same call, runs something that
/// reaches safepoints which it cannot make hold handles instead: an operator that runs passes
/// or the macro evaluator over values it was given (onnx.export_model). It bounds nothing:
/// what is allocated inside the scope is not reclaimed before it closes, so it must not be
/// held around work whose allocation is not bounded by the size of its input, and never
/// around the execution of a program. It is not a substitute for RootHandle.
class NoCollectionScope final {
  public:
    NoCollectionScope() : allocator_(autoSpace()) { allocator_.inhibitCollections(); }
    ~NoCollectionScope() { allocator_.allowCollections(); }
    NoCollectionScope(const NoCollectionScope &)            = delete;
    NoCollectionScope &operator=(const NoCollectionScope &) = delete;

  private:
    GenerationalAllocatorWithGC &allocator_;
};

inline bool autoSpaceSafepointSlowPathEnabled() noexcept {
    return detail::autoSpaceSafepointSlowPath.load(std::memory_order_acquire);
}

inline void safepoint(std::string_view reason = {}) {
    if (autoSpaceSafepointSlowPathEnabled()) {
        autoSpace().safepoint(reason);
    }
}

} // namespace camel::core::mm
