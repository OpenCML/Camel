// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/mm/gen.h"

#include "camel/core/mm/block.h"
#include "camel/core/mm/deferred.h"
#include "camel/core/mm/poison.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/profile/allocation.h"
#include "camel/utils/assert.h"
#include "camel/utils/brpred.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <format>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>

// Core allocation, collection, relocation, and mark/sweep implementation for the
// generational GC. Debug configuration and heap verification live in gc_debug.cpp.

namespace camel::core::mm {

namespace {

constexpr std::string_view kCauseLargeBudget      = "large object budget";
constexpr std::string_view kCauseElderExhausted   = "elder space budget";
constexpr std::string_view kCauseElderBlockBudget = "promoted block budget";
constexpr std::string_view kCauseHeapLimit        = "heap limit";
constexpr const char *kHeapLimitSetting           = "--gc-heap-limit-mb";

double megabytes(uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

} // namespace

GenerationalAllocatorWithGC::GenerationalAllocatorWithGC(const Config &config)
    : birthSpace_(config.birthSize, "auto.birth"), havenSpace_(config.havenSize, "auto.haven"),
      cacheSpace_(config.havenSize, "auto.cache"),
      elderGenSpace_(
          GrowableSpaceConfig{
              .name            = "auto.elder",
              .holds           = "run-time values",
              .segmentBytes    = config.elderGenSize,
              .maxSegmentBytes = 256 * MB,
              // No limit of its own: the heap limit, when one is set, covers the elder and
              // the large-object space together (checkHeapLimitUnlocked).
              .limitBytes       = static_cast<size_t>(1) << 46,
              .limitEnv         = "",
              .sizesFromHeaders = true,
          }),
      largeObjSpace_("auto.large"), promotionAgeThreshold_(config.promotionAgeThreshold),
      largeObjThreshold_(config.largeObjThreshold),
      largeObjMinBudget_(config.largeObjCollectionBudget),
      largeObjGrowthPercent_(config.largeObjGrowthPercent),
      largeObjBudget_(config.largeObjCollectionBudget), elderMinBudget_(config.elderGenSize),
      elderBudget_(config.elderGenSize),
      elderBlockBudget_(config.largeObjCollectionBudget),
      minorTriggerBytes_(
          static_cast<size_t>(
              static_cast<double>(config.birthSize) *
              std::clamp(static_cast<double>(config.minorGCTriggerRatio), 0.1, 1.0))),
      heapLimitBytes_(config.heapLimitBytes), enableYoungGenCopying_(config.enableYoungGenCopying) {
    // Reserve the first elder segment now (untouched pages cost nothing), so the space shows
    // up in snapshots from the start as it did when it was a single block.
    elderGenSpace_.free(elderGenSpace_.alloc(sizeof(slot_t)));
}

void *GenerationalAllocatorWithGC::alloc(size_t payloadSize, size_t align) {
#ifndef NDEBUG
    AllocationDebugScope debugScope;
#endif
    const camel::profile::AllocationScope allocation(payloadSize, false);
    std::lock_guard<std::mutex> lock(mutex_);
    return allocUnlocked(payloadSize, align);
}

void GenerationalAllocatorWithGC::free(void *ptr) {
    (void)ptr;
    ASSERT(false, "GenerationalAllocatorWithGC does not support manual free");
}

void GenerationalAllocatorWithGC::noteFinalizable(void *payload) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enableYoungGenCopying_ && payload && inYoungGenSpace(headerOf(payload))) {
        youngFinalizables_.push_back(static_cast<rtdata::Object *>(payload));
    }
}

void GenerationalAllocatorWithGC::noteBlockHeld(rtdata::Object *owner, Block *block) {
    if (owner == nullptr || block == nullptr) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    // Only a block held by the elder space is beyond a minor collection. A young array that
    // grows its block is counted when it is promoted (forward); a large object is measured by
    // the large-object budget.
    if (!inElderGenSpace(headerOf(owner))) {
        return;
    }
    noteElderBlockBytesUnlocked(block);
}

void GenerationalAllocatorWithGC::noteElderBlockBytesUnlocked(Block *block) {
    if (block == nullptr) {
        return;
    }
    const size_t added = block->claimPacingBytes();
    if (added == 0) {
        return;
    }
    elderBlockBytesSinceSweep_ += added;
    if (UNLIKELY(elderBlockBytesSinceSweep_ >= elderBlockBudget_) &&
        pendingSafepointCollection_ != CollectionKind::Major &&
        pendingSafepointCollection_ != CollectionKind::MinorAndMajor) {
        requestCollectionAtSafepointUnlocked(CollectionKind::Major, kCauseElderBlockBudget);
    }
}

bool GenerationalAllocatorWithGC::isObjectAddressStable(rtdata::Object *object) const {
    if (!object) {
        return true;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ObjectHeader *header = autoHeaderForPayload(object);
    if (header) {
        return inElderGenSpace(header) || inLargeObjSpace(header);
    }

    return graphSpace().contains(object) || permSpace().contains(object);
}

void GenerationalAllocatorWithGC::registerObjectRootSet(
    const void *owner, std::string name, std::vector<rtdata::Object *> *rootSet) {
    std::lock_guard<std::mutex> lock(mutex_);
    ASSERT(owner != nullptr, "An object root set must name its owner.");
    ASSERT(!name.empty(), "An object root set must have a name.");
    auto it = std::find_if(
        objectRootSets_.begin(),
        objectRootSets_.end(),
        [owner](const auto &entry) { return entry.owner == owner; });
    if (it != objectRootSets_.end()) {
        // The same owner re-registering: its entry is updated, nobody else's is touched.
        it->name = std::move(name);
        it->set  = rootSet;
        return;
    }
    objectRootSets_.push_back(ObjectRootSetEntry{owner, std::move(name), rootSet});
}

void GenerationalAllocatorWithGC::unregisterObjectRootSet(const void *owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(
        objectRootSets_, [owner](const auto &entry) { return entry.owner == owner; });
}

void GenerationalAllocatorWithGC::registerGraphSweeper(
    const void *owner, std::string name, std::function<void()> sweeper) {
    std::lock_guard<std::mutex> lock(mutex_);
    ASSERT(owner != nullptr, "A graph sweeper must name its owner.");
    ASSERT(!name.empty(), "A graph sweeper must have a name.");
    ASSERT(static_cast<bool>(sweeper), "A graph sweeper cannot be empty.");
    auto it = std::find_if(
        graphSweepers_.begin(),
        graphSweepers_.end(),
        [owner](const auto &entry) { return entry.owner == owner; });
    if (it != graphSweepers_.end()) {
        it->name    = std::move(name);
        it->sweeper = std::move(sweeper);
        return;
    }
    graphSweepers_.push_back(GraphSweeperEntry{owner, std::move(name), std::move(sweeper)});
}

void GenerationalAllocatorWithGC::unregisterGraphSweeper(const void *owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(graphSweepers_, [owner](const auto &entry) { return entry.owner == owner; });
}

void GenerationalAllocatorWithGC::collectGraphSpace() {
    std::lock_guard<std::mutex> lock(mutex_);
    markPhase();
    for (const auto &entry : graphSweepers_) {
        entry.sweeper();
    }
}

void GenerationalAllocatorWithGC::requestMajorCollection(std::string_view reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    requestCollectionAtSafepointUnlocked(CollectionKind::Major, reason);
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

void GenerationalAllocatorWithGC::linkRootHandle(RootHandle *handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    handle->prev_ = nullptr;
    handle->next_ = rootHandles_;
    if (rootHandles_) {
        rootHandles_->prev_ = handle;
    }
    rootHandles_ = handle;
    ++rootHandleCount_;
}

void GenerationalAllocatorWithGC::unlinkRootHandle(RootHandle *handle) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (handle->prev_) {
        handle->prev_->next_ = handle->next_;
    } else {
        rootHandles_ = handle->next_;
    }
    if (handle->next_) {
        handle->next_->prev_ = handle->prev_;
    }
    handle->prev_ = handle->next_ = nullptr;
    --rootHandleCount_;
}

void GenerationalAllocatorWithGC::forEachRootSourceUnlocked(
    const std::function<void(
        std::string_view name, const std::function<void(const RefRelocator &)> &trace)> &visit) {
    for (const auto &entry : externalRootTracers_) {
        visit(entry.name, entry.tracer);
    }
    for (RootHandle *handle = rootHandles_; handle != nullptr; handle = handle->next_) {
        visit(handle->name_, [handle](const RefRelocator &relocate) { handle->trace(relocate); });
    }
}

void GenerationalAllocatorWithGC::unregisterExternalRootTracer(const void *owner) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(externalRootTracers_, [owner](const auto &entry) {
        return entry.owner == owner;
    });
}

void writeBarrier(
    rtdata::Object *ownerObject, const type::Type *ownerType, slot_t storedSlot,
    const type::Type *storedType) {
    autoSpace().writeBarrier(ownerObject, ownerType, storedSlot, storedType);
}

void writeBarrier(
    rtdata::Object *ownerObject, const type::Type *ownerType, rtdata::Object *storedObject,
    const type::Type *storedType) {
    autoSpace().writeBarrier(ownerObject, ownerType, storedObject, storedType);
}

void GenerationalAllocatorWithGC::writeBarrier(
    rtdata::Object *ownerObject, const type::Type *ownerType, slot_t storedSlot,
    const type::Type *storedType) {
    if (!storedType || !storedType->isGCTraced() || storedSlot == NullSlot) {
        return;
    }
    writeBarrier(
        ownerObject,
        ownerType,
        rtdata::fromSlot<rtdata::Object *>(storedSlot),
        storedType);
}

void GenerationalAllocatorWithGC::writeBarrier(
    rtdata::Object *ownerObject, const type::Type *ownerType, rtdata::Object *storedObject,
    const type::Type *storedType) {
    if (!enableYoungGenCopying_ || !ownerObject || !storedObject || !storedType ||
        !storedType->isGCTraced()) {
        return;
    }
    // The common cases are decided without the lock. The bounds of the young spaces change
    // only inside a collection, when no other thread runs a barrier.
    //  - A young owner needs no entry: the collector reaches it by copying it.
    //  - A target that is neither young nor born old since the last minor collection is not
    //    something a minor collection moves or has to scan.
    const auto isYoungAddress = [this](rtdata::Object *object) {
        return birthSpace_.contains(object) || havenSpace_.contains(object) ||
               cacheSpace_.contains(object);
    };
    if (isYoungAddress(ownerObject)) {
        return;
    }
    if (!isYoungAddress(storedObject) && !anyFreshOld_.load(std::memory_order_acquire)) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.writeBarriers;
    recordObjectTypeUnlocked(ownerObject, ownerType);
    recordObjectTypeUnlocked(storedObject, storedType);

    // A reference to an object born old since the last minor collection counts like one to
    // a young object: that collection finds the object through its owner (see freshOld_).
    ObjectHeader *storedHeader = autoHeaderForPayload(storedObject);
    if (!youngOrFreshUnlocked(storedHeader)) {
        return;
    }

    ObjectHeader *ownerHeader = autoHeaderForPayload(ownerObject);
    if (!ownerHeader || (!inElderGenSpace(ownerHeader) && !inLargeObjSpace(ownerHeader))) {
        return;
    }

    rememberedSet_[ownerHeader] = RememberedSetEntry{
        .object = ownerObject,
        .type   = ownerType,
    };
}

void GenerationalAllocatorWithGC::recordOldToYoungRef(
    rtdata::Object *oldObj, const type::Type *oldType, rtdata::Object *youngObj,
    const type::Type *youngType) {
    ASSERT(youngType != nullptr, "recordOldToYoungRef requires the stored object's precise type.");
    writeBarrier(oldObj, oldType, youngObj, youngType);
}

void GenerationalAllocatorWithGC::publishSafepointSlowPathUnlocked() {
    // The one predicate the schedulers read before they enter the allocator (gen.h).
    detail::autoSpaceSafepointSlowPath.store(
        debugConfig_.stressEveryNAllocations != 0 || debugConfig_.stressEveryNSafepoints != 0 ||
            debugConfig_.poisonAtSafepoints || pendingSafepointCollection_ != CollectionKind::None,
        std::memory_order_release);
}

namespace {

// Marks the allocator as standing at a safepoint for the life of the guard.
class SafepointScope {
  public:
    explicit SafepointScope(bool &flag) : flag_(flag), previous_(flag) { flag_ = true; }
    ~SafepointScope() { flag_ = previous_; }

  private:
    bool &flag_;
    bool previous_;
};

} // namespace

void GenerationalAllocatorWithGC::safepoint(std::string_view reason) {
#ifndef NDEBUG
    // Before the inhibit check and before any collection: the invariant the observer watches
    // is about what is open while Camel code runs, not about whether this safepoint collects.
    observeSafepoint(reason);
    AllocationDebugScope debugScope;
#endif
    CollectionKind requested = CollectionKind::None;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SafepointScope atSafepoint(atSafepoint_);
        ++stats_.safepoints;
        if (debugConfig_.stressEveryNSafepoints != 0 &&
            stats_.safepoints % debugConfig_.stressEveryNSafepoints == 0) {
            requestStressCollectionUnlocked("safepoint interval");
        }
        if (detail::nativeCodeDepth.load(std::memory_order_relaxed) != 0) [[unlikely]] {
            throw std::logic_error(
                std::format(
                    "GC invariant violated: safepoint '{}' reached while JIT-compiled code is "
                    "on the stack (native frames have no stack maps)",
                    reason));
        }
        if (collectionInhibit_.load() != 0) {
            // A NoCollectionScope is open: native code holds raw pointers across this
            // safepoint. Whatever was asked for stays pending.
            ++stats_.inhibitedSafepoints;
            return;
        }
        if (debugConfig_.poisonAtSafepoints) {
            // Every safepoint moves every young object (minorGCUnlocked poisons what they leave).
            requestCollectionAtSafepointUnlocked(CollectionKind::Minor, "safepoint poisoning");
        }

        requested                      = pendingSafepointCollection_;
        pendingSafepointCollection_     = CollectionKind::None;
        const std::string_view gcReason = reason.empty() ? std::string_view{"safepoint"} : reason;
        collectAtSafepointUnlocked(requested, gcReason);
        pendingCause_ = {};
        publishSafepointSlowPathUnlocked();
    }
    // The collection is over and the allocator unlocked: release what the wrappers it
    // found dead were holding (see ForeignResourceObject::finalize), then start the next
    // pacing window from what remains.
    runDeferredReleases();
    if (requested != CollectionKind::None) {
        noteBlockBytesAfterDeferredReleases();
    }
}

void GenerationalAllocatorWithGC::minorGC() {
#ifndef NDEBUG
    observeSafepoint("manual minor GC");
    AllocationDebugScope debugScope;
#endif
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SafepointScope atSafepoint(atSafepoint_);
        ++stats_.safepoints;
        requestCollectionAtSafepointUnlocked(CollectionKind::Minor, "manual minor GC");
        CollectionKind requested    = pendingSafepointCollection_;
        pendingSafepointCollection_ = CollectionKind::None;
        collectAtSafepointUnlocked(requested, "manual minor GC");
        pendingCause_ = {};
        publishSafepointSlowPathUnlocked();
    }
    // The collection is over and the allocator unlocked: release what the wrappers it
    // found dead were holding (see ForeignResourceObject::finalize), then start the next
    // pacing window from what remains.
    runDeferredReleases();
    noteBlockBytesAfterDeferredReleases();
}

void GenerationalAllocatorWithGC::majorGC() {
#ifndef NDEBUG
    observeSafepoint("manual major GC");
    AllocationDebugScope debugScope;
#endif
    {
        std::lock_guard<std::mutex> lock(mutex_);
        SafepointScope atSafepoint(atSafepoint_);
        ++stats_.safepoints;
        requestCollectionAtSafepointUnlocked(CollectionKind::Major, "manual major GC");
        CollectionKind requested    = pendingSafepointCollection_;
        pendingSafepointCollection_ = CollectionKind::None;
        collectAtSafepointUnlocked(requested, "manual major GC");
        pendingCause_ = {};
        publishSafepointSlowPathUnlocked();
    }
    // The collection is over and the allocator unlocked: release what the wrappers it
    // found dead were holding (see ForeignResourceObject::finalize), then start the next
    // pacing window from what remains.
    runDeferredReleases();
    noteBlockBytesAfterDeferredReleases();
}

bool GenerationalAllocatorWithGC::debugRunRememberedSetSelfTest() {
    if (!enableYoungGenCopying_) {
        throw std::runtime_error(
            "GC remembered-set self-test requires the generational collector (--gc generational)");
    }

    auto *stringType = type::Type::String();
    auto *tupleType  = type::TupleType::create(std::vector<type::Type *>{stringType});
    Tuple *owner     = Tuple::create(1, *this);
    RootHandle ownerRoot(*this, owner, tupleType, "GC.remembered_set.selftest.owner");

    for (size_t i = 0; i < promotionAgeThreshold_; ++i) {
        minorGC();
    }

    owner = ownerRoot.getAs<Tuple>();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!inElderGenSpace(autoHeaderForPayload(owner))) {
            throw std::runtime_error("GC remembered-set self-test failed to promote owner object");
        }
    }

    String *child = String::from("remembered-child", *this);
    owner->set<rtdata::Object *>(0, child, tupleType);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (rememberedSet_.empty()) {
            throw std::runtime_error(
                "GC remembered-set self-test did not record old-to-young edge");
        }
    }

    // Two minor collections prove the entry is retained after the first relocation; otherwise the
    // second collection would leave the old tuple pointing into the reset survivor-from space.
    minorGC();
    minorGC();

    owner             = ownerRoot.getAs<Tuple>();
    auto *storedChild = owner->get<String *>(0);
    if (!storedChild || storedChild->view() != "remembered-child") {
        throw std::runtime_error("GC remembered-set self-test lost the young child reference");
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!inYoungGenSpace(autoHeaderForPayload(storedChild))) {
            throw std::runtime_error(
                "GC remembered-set self-test child was not kept in young space");
        }
        if (rememberedSet_.empty()) {
            throw std::runtime_error(
                "GC remembered-set self-test did not retain old-to-young edge");
        }
    }

    auto *arrayType   = type::ArrayType::create(stringType);
    Array *arrayOwner = Array::create(*this, 11);
    RootHandle arrayRoot(*this, arrayOwner, arrayType, "GC.remembered_set.selftest.array_owner");

    for (size_t i = 0; i < promotionAgeThreshold_; ++i) {
        minorGC();
    }

    arrayOwner = arrayRoot.getAs<Array>();
    {
        std::lock_guard<std::mutex> arrayLock(mutex_);
        if (!inElderGenSpace(autoHeaderForPayload(arrayOwner))) {
            throw std::runtime_error(
                "GC remembered-set self-test failed to promote array owner object");
        }
    }

    String *arrayChild = String::from("remembered-array-child", *this);
    arrayOwner->set<rtdata::Object *>(10, arrayChild, arrayType);
    verifyHeapOrThrow("during GC remembered-set self-test array store");

    minorGC();
    (void)String::from("array-overwrite", *this);
    minorGC();

    arrayOwner             = arrayRoot.getAs<Array>();
    auto *storedArrayChild = arrayOwner->get<String *>(10);
    if (!storedArrayChild || storedArrayChild->view() != "remembered-array-child") {
        throw std::runtime_error("GC remembered-set self-test lost the array child reference");
    }

    std::lock_guard<std::mutex> arrayLock(mutex_);
    if (!inYoungGenSpace(autoHeaderForPayload(storedArrayChild))) {
        throw std::runtime_error(
            "GC remembered-set self-test array child was not kept in young space");
    }
    return true;
}

bool GenerationalAllocatorWithGC::debugRunPoisonSelfTest() {
    if (!debugConfig().poisonAtSafepoints) {
        throw std::runtime_error("GC poison self-test requires CAMEL_GC_POISON=1");
    }
    const slot_t poisonWord = poison::word();

    String *text = String::from("poison-selftest", *this);
    RootHandle root(*this, text, type::Type::String(), "GC.poison.selftest.string");
    // What a violation of the handle rules looks like: the address, kept in a C++ variable.
    const auto *stale = reinterpret_cast<const slot_t *>(text);

    safepoint("GC poison self-test");

    String *moved = root.getAs<String>();
    if (moved == text) {
        throw std::runtime_error("GC poison self-test: the safepoint did not move a young object");
    }
    if (moved->view() != "poison-selftest") {
        throw std::runtime_error("GC poison self-test: the rooted object lost its contents");
    }
    // The first word of an object is its vtable pointer: a virtual call through the stale
    // address would load the poison and fault in the guard page.
    if (stale[0] != poisonWord || !poison::isPoison(stale[0])) {
        throw std::runtime_error(
            "GC poison self-test: the address an object moved away from was not poisoned");
    }

    // The addresses stay poisoned over the following safepoints, not just the first.
    for (int i = 0; i < 8; ++i) {
        (void)String::from("poison-selftest-filler", *this);
        safepoint("GC poison self-test");
        if (stale[0] != poisonWord) {
            throw std::runtime_error(
                "GC poison self-test: an evacuated address was handed out again too early");
        }
    }
    if (root.getAs<String>()->view() != "poison-selftest") {
        throw std::runtime_error("GC poison self-test: the rooted object lost its contents");
    }
    return true;
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
        return allocLargeUnlocked(payloadSize, align);
    }

    if (!enableYoungGenCopying_) {
        return allocElderUnlocked(payloadSize, align);
    }

    // Try allocating in the birth space first.
    void *ptr               = birthSpace_.alloc(payloadSize, align);
    const bool minorPending = pendingSafepointCollection_ == CollectionKind::Minor ||
                              pendingSafepointCollection_ == CollectionKind::MinorAndMajor;
    if (UNLIKELY(!ptr)) {
        // No room, and an allocation site is no safepoint: the object is born old (and
        // tracked as such until the minor collection that the next safepoint runs).
        if (!minorPending) {
            requestCollectionAtSafepointUnlocked(CollectionKind::Minor, "birth space full");
        }
        return allocElderUnlocked(payloadSize, align);
    }

    stats_.youngBytesAllocated += payloadSize;
    headerOf(ptr)->setRegion(AllocRegion::YoungGen);
    if (UNLIKELY(
            !minorPending &&
            birthSpace_.used() + blockBytesGrowthUnlocked() >= minorTriggerBytes_)) {
        requestCollectionAtSafepointUnlocked(CollectionKind::Minor, "birth space nearly full");
    }
    return ptr;
}

size_t GenerationalAllocatorWithGC::oldBytesHeldUnlocked() const {
    return elderGenSpace_.usage().usedBytes + largeLiveBytes_ + largeBytesSinceSweep_ +
           camel::core::mm::blockBytesLive();
}

size_t GenerationalAllocatorWithGC::blockBytesGrowthUnlocked() const {
    const size_t live = camel::core::mm::blockBytesLive();
    return live > blockBytesAtCollection_ ? live - blockBytesAtCollection_ : 0;
}

void GenerationalAllocatorWithGC::checkBlockBytes(size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    checkHeapLimitUnlocked(bytes);
}

void GenerationalAllocatorWithGC::checkHeapLimitUnlocked(size_t requestBytes) {
    if (LIKELY(heapLimitBytes_ == 0)) {
        return;
    }
    const size_t held = oldBytesHeldUnlocked();
    if (LIKELY(held + requestBytes <= heapLimitBytes_)) {
        return;
    }
    // Over the limit. Part of what is held may be garbage, but nothing can be collected here
    // (an allocation site is no safepoint), so the allocation is let through and a major
    // collection is asked for; the safepoint that runs it fails if the limit is still
    // exceeded afterwards. What is refused on the spot is what no collection can make fit (a
    // request larger than the limit) and a run past one and a half times the limit before any
    // safepoint is reached.
    if (requestBytes > heapLimitBytes_ ||
        held + requestBytes > heapLimitBytes_ + heapLimitBytes_ / 2) {
        throw MemoryExhausted(
            "auto",
            "run-time values",
            requestBytes,
            held,
            heapLimitBytes_,
            kHeapLimitSetting);
    }
    if (!heapLimitExceeded_) {
        heapLimitExceeded_ = true;
        heapLimitRequest_  = requestBytes;
        requestCollectionAtSafepointUnlocked(CollectionKind::Major, kCauseHeapLimit);
    }
}

void *GenerationalAllocatorWithGC::allocLargeUnlocked(size_t payloadSize, size_t align) {
    checkHeapLimitUnlocked(payloadSize);
    // The large-object space takes its blocks from the host and has no limit of its own: a
    // failure here is the host running out of memory. It is told as such, with the volume the
    // space holds, instead of as a bare std::bad_alloc.
    void *ptr = nullptr;
    try {
        ptr = largeObjSpace_.alloc(payloadSize, align);
    } catch (const std::bad_alloc &) {
        ptr = nullptr;
    }
    if (UNLIKELY(!ptr)) {
        throw MemoryExhausted(
            "auto",
            "run-time values",
            payloadSize,
            oldBytesHeldUnlocked(),
            0,
            {});
    }
    auto *header = headerOf(ptr);
    header->setRegion(AllocRegion::LargeObj);
    objectTypes_.erase(header);
    if (enableYoungGenCopying_) {
        freshOld_.insert(header);
        anyFreshOld_.store(true, std::memory_order_release);
    }
    // Large objects never exhaust a space, so pace their reclamation by volume:
    // once the budget is spent, ask for a major collection at the next safepoint.
    largeBytesSinceSweep_ += payloadSize;
    stats_.largeBytesAllocated += payloadSize;
    stats_.largeHighWaterMark =
        std::max(stats_.largeHighWaterMark, largeLiveBytes_ + largeBytesSinceSweep_);
    if (largeBytesSinceSweep_ + blockBytesGrowthUnlocked() >= largeObjBudget_) {
        requestCollectionAtSafepointUnlocked(CollectionKind::Major, kCauseLargeBudget);
    }
    return ptr;
}

void *GenerationalAllocatorWithGC::allocElderUnlocked(size_t payloadSize, size_t align) {
    checkHeapLimitUnlocked(payloadSize);
    // The elder space grows by segments when no block fits, so this neither fails (short of
    // the host refusing memory, which the space reports as MemoryExhausted) nor collects:
    // an allocation site is not a safepoint, and the caller can hold objects that no root
    // reaches yet. Array::create, for one, allocates the Array and then its backing store; a
    // sweep during the second allocation would free the first.
    //
    // Collections are paced by volume instead, as for large objects: once the space took its
    // budget since the last sweep, a major collection is asked for at the next safepoint. The
    // budget follows what the sweep left alive (sweepOldGen), so a program whose elder space
    // is full of live objects is not marked again and again for nothing.
    void *ptr = elderGenSpace_.alloc(payloadSize, align);
    elderBytesSinceSweep_ += payloadSize;
    stats_.elderBytesAllocated += payloadSize;
    auto *header = headerOf(ptr);
    header->setRegion(AllocRegion::ElderGen);
    objectTypes_.erase(header);
    if (enableYoungGenCopying_) {
        freshOld_.insert(header);
        anyFreshOld_.store(true, std::memory_order_release);
    }
    if (UNLIKELY(
            elderBytesSinceSweep_ + blockBytesGrowthUnlocked() >= elderBudget_) &&
        pendingSafepointCollection_ != CollectionKind::Major &&
        pendingSafepointCollection_ != CollectionKind::MinorAndMajor) {
        requestCollectionAtSafepointUnlocked(CollectionKind::Major, kCauseElderExhausted);
    }
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
    requestCollectionAtSafepointUnlocked(debugConfig_.stressCollection, reason);
}

void GenerationalAllocatorWithGC::requestCollectionAtSafepointUnlocked(
    CollectionKind kind, std::string_view reason) {
    if (kind == CollectionKind::None) {
        return;
    }
    ++stats_.deferredCollections;
    if (pendingSafepointCollection_ == CollectionKind::None) {
        pendingCause_ = reason;
        if (reason == kCauseLargeBudget) {
            ++stats_.largeBudgetRequests;
        } else if (reason == kCauseElderExhausted) {
            ++stats_.elderExhaustedRequests;
        } else if (reason == kCauseElderBlockBudget) {
            ++stats_.elderBlockBudgetRequests;
        } else {
            ++stats_.otherRequests;
        }
    }
    pendingSafepointCollection_ = combineCollectionKinds(pendingSafepointCollection_, kind);
    // Allocation and stress sites only request moving-capable GC. The actual collection must wait
    // for an explicit safepoint where roots can be relocated, so publish that pending work to
    // scheduler-level safepoint polls.
    detail::autoSpaceSafepointSlowPath.store(true, std::memory_order_release);
}

void GenerationalAllocatorWithGC::collectAtSafepointUnlocked(
    CollectionKind kind, std::string_view reason) {
    if (kind == CollectionKind::None) {
        return;
    }
    // The invariant every holder of a raw object pointer relies on: objects are moved and
    // freed at safepoints only. Checked in release builds too; it costs one test per
    // collection.
    if (!atSafepoint_) [[unlikely]] {
        throw std::logic_error(
            std::format(
                "GC invariant violated: a collection ({}) was started outside a safepoint",
                reason));
    }
    ++stats_.requestedCollections;

    if (debugConfig_.verifyBeforeGC) {
        throwIfVerificationFailed(verifyHeapUnlocked(), std::format("before {}", reason));
    }

    const auto begin = std::chrono::steady_clock::now();
    try {
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
    } catch (...) {
        // A collection that throws stops half way: the haven has been swapped, some objects carry
        // forwarding pointers and the remembered set has been rewritten. The heap is not a heap
        // any more, and the exception travels into ordinary fault handling (`attempt`, a macro
        // evaluation, a per-element fault boundary) as if the program could go on (C24, MG/R8).
        // The one thing a half-relocated heap can say is that it stopped, so it is fatal here, and
        // the diagnostics of the process can name what was being collected.
        reportFatalCollectionFailure(kind, reason);
    }
    {
        const auto pause =
            static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now() - begin)
                                      .count());
        if (kind == CollectionKind::Minor) {
            stats_.minorPauseNs += pause;
            stats_.minorPauseMaxNs = std::max(stats_.minorPauseMaxNs, pause);
        } else {
            stats_.majorPauseNs += pause;
            stats_.majorPauseMaxNs = std::max(stats_.majorPauseMaxNs, pause);
        }
    }
    // The pacing base is *not* taken here: the finalizers this collection ran only queued the
    // blocks of the arrays they found dead (see Block::releaseFromFinalizer), and those bytes
    // are still live right now. Taking the base before the queue runs would pace the next
    // window against a volume that includes this collection's own garbage; with a workload that
    // recycles the same working set the trigger then needs more and more growth to fire, and the
    // high-water mark climbs as if nothing were ever released. The callers take the base after
    // runDeferredReleases() (see noteBlockBytesAfterDeferredReleases).

    if (heapLimitBytes_ != 0 && kind == CollectionKind::Minor &&
        oldBytesHeldUnlocked() > heapLimitBytes_ && !heapLimitExceeded_) {
        // Promotion took the old spaces over the limit (it cannot be refused in the middle of
        // a collection). Part of what they hold may be garbage: a major collection decides.
        heapLimitExceeded_ = true;
        heapLimitRequest_  = oldBytesHeldUnlocked() - heapLimitBytes_;
        requestCollectionAtSafepointUnlocked(CollectionKind::Major, kCauseHeapLimit);
    }
    if (heapLimitBytes_ != 0 && kind != CollectionKind::Minor) {
        // A major collection has run. What the old spaces still hold is alive; if that alone
        // exceeds the limit the program does not fit.
        heapLimitExceeded_ = false;
        if (const size_t held = oldBytesHeldUnlocked(); held > heapLimitBytes_) {
            throw MemoryExhausted(
                "auto",
                "run-time values",
                heapLimitRequest_,
                held,
                heapLimitBytes_,
                kHeapLimitSetting);
        }
    }

    if (debugConfig_.verifyAfterGC) {
        throwIfVerificationFailed(verifyHeapUnlocked(), std::format("after {}", reason));
    }
}

void GenerationalAllocatorWithGC::noteBlockBytesAfterDeferredReleases() {
    // Runs on the collecting thread after the deferred queue has been drained, so the blocks the
    // collection found dead are already out of the live count. Re-read it rather than subtracting
    // per-release amounts: the queue also runs releases queued by earlier work.
    std::lock_guard<std::mutex> lock(mutex_);
    blockBytesAtCollection_ = camel::core::mm::blockBytesLive();
}

void GenerationalAllocatorWithGC::reportFatalCollectionFailure(
    CollectionKind kind, std::string_view reason) {
    const char *what = kind == CollectionKind::Minor   ? "minor"
                       : kind == CollectionKind::Major ? "major"
                                                       : "minor and major";
    try {
        std::string detail = "an unnamed exception";
        if (std::exception_ptr current = std::current_exception()) {
            try {
                std::rethrow_exception(current);
            } catch (const std::exception &error) {
                detail = error.what();
            } catch (...) {
                // Keep the fallback text.
            }
        }
        std::cerr << std::format(
            "camel: fatal: the {} collection started for '{}' threw ({}); the heap is half "
            "relocated and cannot be used again\n",
            what,
            reason,
            detail);
        std::cerr.flush();
    } catch (...) {
        // Reporting must not hide the stop.
    }
    std::terminate();
}

void GenerationalAllocatorWithGC::minorGCUnlocked() {
    if (!enableYoungGenCopying_) {
        return;
    }
    // Reentrancy is a programming error, not a state to carry on from: nothing that runs inside a
    // collection allocates through this allocator, and a collection only starts at a safepoint, so
    // this means a caller collected from inside one. Returning here (as this used to) skipped a
    // requested collection without saying so (C24, MG/R8).
    ASSERT(!inGC_, "A minor collection was started while one was already running.");
    if (inGC_) {
        throw std::logic_error(
            "GC invariant violated: a minor collection was started while one was running");
    }
    inGC_ = true;
    ++stats_.minorCollections;

    try {
        relocationScan_.clear();
        relocationScanIndex_ = 0;

        // 1. Swap Cache and Haven
        cacheSpace_.swap(havenSpace_);
        if (debugConfig_.poisonAtSafepoints) {
            // The new To space was poisoned when it was emptied; keep that poison in place
            // for as long as the survivors fit above it.
            havenSpace_.rewindIfShort(birthSpace_.used() + cacheSpace_.used());
        } else {
            havenSpace_.reset(); // Clear the new Haven (To) space
        }

        minorTracedGraphs_.clear();
        minorGraphScan_.clear();
        auto relocateYoung =
            [this](rtdata::Object *ref, const type::Type *refType, const rtdata::RefTraceInfo &info)
            -> rtdata::Object * { return relocateForMinor(ref, refType, info); };

        // 2. Forward young-gen objects referenced from roots. The root set currently stores
        // runtime GCGraph carriers, which live in graphSpace rather than autoSpace; tracing the
        // root object's fields is therefore required to update static data roots.
        if (!objectRootSets_.empty()) {
            for (const ObjectRootSetEntry &entry : objectRootSets_) {
                if (entry.set == nullptr) {
                    continue;
                }
                for (rtdata::Object *&rootObj : *entry.set) {
                    if (!rootObj) {
                        continue;
                    }

                    rootObj = forwardYoungRef(rootObj, nullptr);
                    if (minorTracedGraphs_.insert(rootObj).second) {
                        rootObj->updateRefs(relocateYoung, nullptr);
                    }
                }
            }
        }
        for (const auto &entry : externalRootTracers_) {
            entry.tracer(relocateYoung);
        }
        for (RootHandle *handle = rootHandles_; handle != nullptr; handle = handle->next_) {
            handle->trace(relocateYoung);
        }

        // 3. Process old-to-young references. Entries are retained after relocation while the
        // old object still points into the young generation; clearing them blindly would lose the
        // edge on the next minor collection.
        std::unordered_map<ObjectHeader *, RememberedSetEntry> retainedRememberedSet;
        for (const auto &[oldHeader, entry] : rememberedSet_) {
            if (!oldHeader || !oldHeader->isValid())
                continue;

            rtdata::Object *oldObj =
                entry.object ? entry.object : payloadOf<rtdata::Object>(oldHeader);
            const type::Type *oldType =
                entry.type ? entry.type : knownObjectTypeUnlocked(oldHeader);

            // Walk and update refs in old-gen objects (layout is per Object / allocator contract).
            oldObj->updateRefs(relocateYoung, oldType);
            recordObjectTypeUnlocked(oldObj, oldType);
            retainedRememberedSet[oldHeader] = RememberedSetEntry{
                .object = oldObj,
                .type   = oldType,
            };
        }

        // 4. Scan copied/promoted objects, objects born old and late-found graphs with their
        // static layouts.
        scanRelocatedObjects();
        // From here on "young" means young only: nothing is fresh any more.
        freshOld_.clear();
        anyFreshOld_.store(false, std::memory_order_release);
        // Keep the remembered owners that still point at a young object (the scan above may
        // have promoted their targets), and remember the promoted and born-old objects that do.
        rememberedSet_.clear();
        for (const auto &[oldHeader, entry] : retainedRememberedSet) {
            if (hasYoungRefsUnlocked(entry.object, entry.type)) {
                rememberedSet_[oldHeader] = entry;
            }
        }
        for (const TraceEntry &entry : relocationScan_) {
            rememberOldObjectIfYoungRefsUnlocked(entry.object, entry.type);
        }
        minorTracedGraphs_.clear();
        minorGraphScan_.clear();

        // 5. Finalize dead young wrappers, then reset Birth and Cache. Forwarded source objects
        // still have a live copy in Haven/Elder, so finalizing only unforwarded payloads preserves
        // the "moving the wrapper does not retain/release the native resource" contract.
        finalizeDeadYoungUnlocked();
        if (debugConfig_.poisonAtSafepoints) {
            // Whatever still points at the objects that were here reads the poison word from
            // now on, and the addresses are not handed out again while a quarter of the space
            // remains above them.
            const slot_t poisonWord = poison::word();
            birthSpace_.resetPoisoned(poisonWord, birthSpace_.capacity() / 4);
            cacheSpace_.resetPoisoned(poisonWord, 0);
        } else {
            birthSpace_.reset();
            cacheSpace_.reset();
        }

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

    if (enableYoungGenCopying_) {
        // Moving major collection must first relocate young objects into their final locations.
        // Marking before promotion can leave newly promoted elder objects unmarked and therefore
        // sweep live objects. After the minor pass, roots and remembered-set edges point at the
        // final young/elder objects, so the full mark phase sees the heap shape that sweep will
        // use.
        minorGCUnlocked();
    }

    // Mark all reachable objects in their final locations for this major cycle.
    markPhase();

    // The graph space holds Programs and graph versions: the unmarked ones are freed, by every
    // owner that registered a sweeper (one context's collection keeps every context's graphs).
    for (const GraphSweeperEntry &entry : graphSweepers_) {
        entry.sweeper();
    }

    // Sweep old generation (mark-sweep; no compaction here).
    sweepOldGen();

    // Sweep large-object space.
    sweepLargeObjects();
}

ObjectHeader *GenerationalAllocatorWithGC::autoHeaderForPayload(rtdata::Object *object) const {
    if (!object) {
        return nullptr;
    }
    if (birthSpace_.contains(object) || havenSpace_.contains(object) ||
        cacheSpace_.contains(object) || elderGenSpace_.contains(object) ||
        largeObjSpace_.contains(object)) {
        return headerOf(object);
    }
    return nullptr;
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

bool GenerationalAllocatorWithGC::isPreciseGCType(const type::Type *objectType) {
    return objectType && objectType->isGCTraced() && objectType->code() != type::TypeCode::Ref;
}

void GenerationalAllocatorWithGC::recordObjectTypeUnlocked(
    rtdata::Object *object, const type::Type *objectType) {
    // The table serves the heap verifier (and is a fallback for a remembered owner that came
    // without a layout). Filling it costs a hash insertion per object marked or promoted, so
    // it is filled only once verification has been asked for.
    if (!recordLayouts_ || !isPreciseGCType(objectType) || !object) {
        return;
    }
    // Layouts are kept for old objects only (the remembered set and the verifier ask for
    // them). A young object is always reached with its layout, and keeping one entry per
    // young object made every minor collection pay for the dead ones too.
    if (birthSpace_.contains(object) || havenSpace_.contains(object) ||
        cacheSpace_.contains(object)) {
        return;
    }
    if (ObjectHeader *header = autoHeaderForPayload(object)) {
        objectTypes_[header] = objectType;
    }
}

const type::Type *GenerationalAllocatorWithGC::knownObjectTypeUnlocked(ObjectHeader *header) const {
    auto it = objectTypes_.find(header);
    return it == objectTypes_.end() ? nullptr : it->second;
}

std::vector<GenerationalAllocatorWithGC::RememberedEdge>
GenerationalAllocatorWithGC::collectYoungReferenceEdges(
    rtdata::Object *object, const type::Type *objectType) const {
    std::vector<RememberedEdge> edges;
    ObjectHeader *ownerHeader = autoHeaderForPayload(object);
    if (!ownerHeader || !object) {
        return edges;
    }

    object->updateRefs(
        [&](rtdata::Object *ref,
            const type::Type *refType,
            const rtdata::RefTraceInfo &info) -> rtdata::Object * {
            if (ref && youngOrFreshUnlocked(autoHeaderForPayload(ref))) {
                edges.push_back(
                    RememberedEdge{
                        .ownerHeader = ownerHeader,
                        .owner       = object,
                        .ownerType   = objectType,
                        .target      = ref,
                        .slotType    = refType,
                        .info        = info,
                    });
            }
            return ref;
        },
        objectType);
    return edges;
}

bool GenerationalAllocatorWithGC::hasYoungRefsUnlocked(
    rtdata::Object *object, const type::Type *objectType) const {
    bool found = false;
    object->updateRefs(
        [&](rtdata::Object *ref,
            const type::Type *,
            const rtdata::RefTraceInfo &) -> rtdata::Object * {
            if (!found && ref) {
                // Range checks first: the lookup of a large object takes that space's lock.
                found = birthSpace_.contains(ref) || havenSpace_.contains(ref) ||
                        cacheSpace_.contains(ref) ||
                        (!freshOld_.empty() && freshOld_.contains(headerOf(ref)));
            }
            return ref;
        },
        objectType);
    return found;
}

void GenerationalAllocatorWithGC::rememberOldObjectIfYoungRefsUnlocked(
    rtdata::Object *object, const type::Type *objectType) {
    // Most of what a minor collection copies stays young: decided by three range checks.
    if (!object || birthSpace_.contains(object) || havenSpace_.contains(object) ||
        cacheSpace_.contains(object)) {
        return;
    }
    ObjectHeader *header = autoHeaderForPayload(object);
    if (!header || (!inElderGenSpace(header) && !inLargeObjSpace(header))) {
        return;
    }
    recordObjectTypeUnlocked(object, objectType);
    if (hasYoungRefsUnlocked(object, objectType)) {
        rememberedSet_[header] = RememberedSetEntry{
            .object = object,
            .type   = objectType,
        };
    }
}

void GenerationalAllocatorWithGC::finalizeDeadYoungUnlocked() {
    // Before the From spaces are reset: their headers still say what was copied. A copied
    // wrapper is the same wrapper (the copy owns the resource; nothing is retained or
    // released by the move) and stays on the list while it is young.
    std::vector<rtdata::Object *> stillYoung;
    for (rtdata::Object *object : youngFinalizables_) {
        ObjectHeader *header = headerOf(object);
        if (!header->forwarded()) {
            object->finalize();
            continue;
        }
        auto *moved = static_cast<rtdata::Object *>(header->forwardedAddr());
        if (inYoungGenSpace(headerOf(moved))) {
            stillYoung.push_back(moved);
        }
    }
    youngFinalizables_.swap(stillYoung);
}

void GenerationalAllocatorWithGC::finalizeObjects(const std::vector<ObjectHeader *> &objects) {
    for (ObjectHeader *header : objects) {
        if (!header) {
            continue;
        }
        payloadOf<rtdata::Object>(header)->finalize();
    }
}

rtdata::Object *
GenerationalAllocatorWithGC::forward(rtdata::Object *obj, const type::Type *objType) {
    ObjectHeader *header = headerOf(obj);
    ASSERT(header->isValid(), "Invalid ObjectHeader encountered during forwarding");

    // Already forwarded: return the forwardee.
    if (header->forwarded()) {
        auto *forwarded = static_cast<rtdata::Object *>(header->forwardedAddr());
        recordObjectTypeUnlocked(forwarded, objType);
        return forwarded;
    }

    size_t objSize = header->objSize();

    // Bump survival age.
    header->incAge();
    uint64_t age = header->age();

    void *newObj            = nullptr;
    ObjectHeader *newHeader = nullptr;

    // Promotion cannot fail short of the host refusing memory (the elder space grows), and
    // must not collect: a collection is already running. What it takes counts against the
    // elder budget, so a major collection follows at a later safepoint.
    const auto promote = [&]() {
        newObj = elderGenSpace_.alloc(objSize, alignof(slot_t));
        elderBytesSinceSweep_ += objSize;
        if (UNLIKELY(
                elderBytesSinceSweep_ + blockBytesGrowthUnlocked() >= elderBudget_) &&
            pendingSafepointCollection_ != CollectionKind::Major &&
            pendingSafepointCollection_ != CollectionKind::MinorAndMajor) {
            requestCollectionAtSafepointUnlocked(CollectionKind::Major, kCauseElderExhausted);
        }
        // A block this value holds moves with it into the elder space, and only a major
        // collection sweeps the elder space, so these bytes can no longer be reclaimed by the
        // minor collections the blocks pace. Count them and ask for the major when enough have
        // accumulated since the last sweep. The block is counted by identity: a second view of
        // it that is promoted later adds nothing.
        noteElderBlockBytesUnlocked(obj->heldBlock());
        newHeader = headerOf(newObj);
        newHeader->setAge(age);
        newHeader->setRegion(AllocRegion::ElderGen);
        objectTypes_.erase(newHeader);
    };

    bool promoted = false;
    if (UNLIKELY(age >= promotionAgeThreshold_)) {
        promote();
        promoted = true;
    } else {
        // Copy into survivor To; when it is full, promote instead.
        newObj = havenSpace_.alloc(objSize, alignof(slot_t));
        if (UNLIKELY(!newObj)) {
            promote();
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
    recordObjectTypeUnlocked(gcObj, objType);
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

bool GenerationalAllocatorWithGC::youngOrFreshUnlocked(ObjectHeader *header) const {
    return header &&
           (inYoungGenSpace(header) || (!freshOld_.empty() && freshOld_.contains(header)));
}

rtdata::Object *GenerationalAllocatorWithGC::relocateForMinor(
    rtdata::Object *ref, const type::Type *refType, const rtdata::RefTraceInfo &info) {
    if (!ref) {
        return nullptr;
    }
    if (refType == nullptr &&
        (info.slotName == "graph" || info.slotName == "graph-edge" || info.slotName == "program")) {
        // A graph carrier (graph space, never moved). Program graphs are roots and were
        // traced at the start; a graph created while the program runs (an autodiff gradient
        // graph, an attached rule) is reached only here, through the Function that owns it or
        // through another graph. Its static area can hold young constants, so it is traced
        // once per collection like a root.
        //
        // The Function may itself be dead: a minor collection also follows what a dead but
        // not yet swept old object refers to. Its graph can then have been released already
        // (GCGraphManager frees the graphs a rewrite replaced), so the carrier is traced only
        // if the graph space still holds it. A new graph at the same address is a live graph,
        // and tracing a live graph once more is harmless.
        if (graphSpace().isAllocated(ref) && minorTracedGraphs_.insert(ref).second) {
            minorGraphScan_.push_back(ref);
        }
        return ref;
    }
    return forwardYoungRef(ref, refType);
}

rtdata::Object *
GenerationalAllocatorWithGC::forwardYoungRef(rtdata::Object *ref, const type::Type *refType) {
    if (!ref) {
        return nullptr;
    }
    ObjectHeader *refHeader = headerOf(ref);
    // Everything the To space holds was copied there by this collection and is final. A slot
    // can be visited more than once (the static area of a graph is traced as a graph root and
    // again through every active frame of that graph): copying the copy would leave the first
    // one behind as a forwarded husk in the To space, and the next collection would follow
    // its forwarding pointer into the space it is evacuating.
    if (havenSpace_.contains(refHeader)) {
        return ref;
    }
    if (inYoungGenSpace(refHeader)) {
        return forward(ref, refType);
    }
    if (UNLIKELY(!freshOld_.empty())) {
        // Born old since the last minor collection: it does not move, but nothing recorded
        // what it points at. Scan it. With a precise layout once is enough; without one (a
        // self-describing object, or an untyped reference) it is scanned through every
        // reference that reaches it, and a later typed one still finds it here.
        if (auto it = freshOld_.find(refHeader); it != freshOld_.end()) {
            const type::Type *type =
                isPreciseGCType(refType) ? refType : knownObjectTypeUnlocked(refHeader);
            if (isPreciseGCType(type)) {
                freshOld_.erase(it);
                recordObjectTypeUnlocked(ref, type);
            }
            relocationScan_.push_back(TraceEntry{ref, type ? type : refType});
        }
    }
    return ref;
}

void GenerationalAllocatorWithGC::scanRelocatedObjects() {
    const auto relocate =
        [this](rtdata::Object *ref, const type::Type *refType, const rtdata::RefTraceInfo &info)
        -> rtdata::Object * { return relocateForMinor(ref, refType, info); };
    for (;;) {
        if (relocationScanIndex_ < relocationScan_.size()) {
            TraceEntry entry = relocationScan_[relocationScanIndex_++];
            if (!entry.object) {
                continue;
            }
            // Without a layout the composite values trace nothing, but objects that describe
            // themselves (a Function, a tensor view, an optimizer state) do, and must: they
            // were just copied and still point at where their referents were.
            entry.object->updateRefs(relocate, entry.type);
            continue;
        }
        if (!minorGraphScan_.empty()) {
            rtdata::Object *graph = minorGraphScan_.back();
            minorGraphScan_.pop_back();
            graph->updateRefs(relocate, nullptr);
            continue;
        }
        break;
    }
}

void GenerationalAllocatorWithGC::markPhase() {
    clearMarks();

    if (!objectRootSets_.empty()) {
        for (const ObjectRootSetEntry &entry : objectRootSets_) {
            if (entry.set == nullptr) {
                continue;
            }
            for (rtdata::Object *root : *entry.set) {
                if (root) {
                    markObject(root, nullptr);
                }
            }
        }
    }
    const RefRelocator markRef = [this](
                                     rtdata::Object *ref,
                                     const type::Type *refType,
                                     const rtdata::RefTraceInfo &) -> rtdata::Object * {
        if (ref) {
            markObject(ref, refType);
        }
        return ref;
    };
    for (const auto &entry : externalRootTracers_) {
        entry.tracer(markRef);
    }
    for (RootHandle *handle = rootHandles_; handle != nullptr; handle = handle->next_) {
        handle->trace(markRef);
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

    // Graph carriers are also traced through Function values (Function::updateRefs traces the
    // runtime graph so runtime-created graphs keep their static constants alive). Those graphs
    // are not necessarily in a registered root set, so unmark the whole graph space instead of only
    // the registered roots, or a later collection would skip them and miss their edges.
    graphSpace().iterateAllocated([](ObjectHeader *header) { header->unmark(); });

    for (const ObjectRootSetEntry &entry : objectRootSets_) {
        if (entry.set == nullptr) {
            continue;
        }
        for (rtdata::Object *root : *entry.set) {
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
        recordObjectTypeUnlocked(current, entry.type);

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
    size_t liveBlockBytes = 0;
    const uint64_t sweepGeneration = blockSweepGeneration_++;

    elderGenSpace_.iterateAllocated(
        [&unreachable, &liveBlockBytes, sweepGeneration](ObjectHeader *header) {
            if (!header->marked_) {
                unreachable.push_back(header);
            } else {
                // The blocks the survivors still hold: only a major collection can release
                // them, so they are the base of the next promoted-block budget. A block can be
                // reached through several views; count it once, by identity.
                Block *block = payloadOf<rtdata::Object>(header)->heldBlock();
                if (block != nullptr && block->claimSweepGeneration(sweepGeneration)) {
                    liveBlockBytes += block->bytes();
                }
            }
        });

    stats_.freedElderObjects += unreachable.size();
    finalizeObjects(unreachable);
    for (ObjectHeader *header : unreachable) {
        rememberedSet_.erase(header);
        objectTypes_.erase(header);
    }
    elderGenSpace_.freeBulk(unreachable);
    // The next budget: the floor, or the growth percentage of what is left alive (the rule
    // of the large-object space, see sweepLargeObjects).
    elderBytesSinceSweep_ = 0;
    elderLiveBytes_       = elderGenSpace_.usage().usedBytes;
    const size_t growth   = elderLiveBytes_ / 100 * largeObjGrowthPercent_ +
                            elderLiveBytes_ % 100 * largeObjGrowthPercent_ / 100;
    elderBudget_          = std::max(elderMinBudget_, growth);
    // The same rule for the blocks held by promoted values: the floor, or the growth
    // percentage of the block bytes this sweep left in the elder space. The bytes promoted
    // since the sweep are what can have turned into garbage that only the next major can free.
    elderBlockBytesSinceSweep_ = 0;
    elderBlockLiveBytes_       = liveBlockBytes;
    const size_t blockGrowth = liveBlockBytes / 100 * largeObjGrowthPercent_ +
                               liveBlockBytes % 100 * largeObjGrowthPercent_ / 100;
    elderBlockBudget_ = std::max(largeObjMinBudget_, blockGrowth);
}

void GenerationalAllocatorWithGC::sweepLargeObjects() {
    std::vector<ObjectHeader *> unreachable;

    largeObjSpace_.iterateAllocated([&unreachable](ObjectHeader *header) {
        if (!header->marked_) {
            unreachable.push_back(header);
        }
    });

    stats_.freedLargeObjects += unreachable.size();
    finalizeObjects(unreachable);
    for (ObjectHeader *header : unreachable) {
        rememberedSet_.erase(header);
        objectTypes_.erase(header);
    }
    largeObjSpace_.freeBulk(unreachable);

    // The next budget scales with the surviving large-object volume, so the amortized cost of
    // collecting stays proportional to allocation: with a growth of g percent a program whose
    // live volume is L is collected once per L * g / 100 bytes it allocates, and holds at most
    // L * (1 + g / 100) plus what it allocates between the request and the next safepoint.
    size_t liveBytes   = 0;
    size_t liveObjects = 0;
    largeObjSpace_.iterateAllocated([&](ObjectHeader *header) {
        liveBytes += header->size();
        ++liveObjects;
    });
    const size_t heldBefore   = largeLiveBytes_ + largeBytesSinceSweep_;
    const size_t sinceSweep   = largeBytesSinceSweep_;
    const size_t budgetBefore = largeObjBudget_;
    largeBytesSinceSweep_     = 0;
    largeLiveBytes_           = liveBytes;
    // live * percent / 100, split so that it neither overflows nor rounds (100 gives live).
    const size_t growth =
        liveBytes / 100 * largeObjGrowthPercent_ + liveBytes % 100 * largeObjGrowthPercent_ / 100;
    largeObjBudget_ = std::max(largeObjMinBudget_, growth);
    if (statsLevel_ >= 2) {
        std::cerr << std::format(
            "[camel] gc collection: n={} cause=\"{}\" budget_mb={:.1f} since_sweep_mb={:.1f} "
            "large_before_mb={:.1f} large_live_mb={:.1f} large_live_objects={} "
            "freed_large_objects={} next_budget_mb={:.1f}\n",
            stats_.majorCollections,
            pendingCause_.empty() ? std::string_view{"explicit"} : pendingCause_,
            megabytes(budgetBefore),
            megabytes(sinceSweep),
            megabytes(heldBefore),
            megabytes(liveBytes),
            liveObjects,
            unreachable.size(),
            megabytes(largeObjBudget_));
    }
}

void GenerationalAllocatorWithGC::setStatsLevel(int level) {
    std::lock_guard<std::mutex> lock(mutex_);
    statsLevel_ = level;
}

void GenerationalAllocatorWithGC::reportStats(std::ostream &os) const {
    const RuntimeStats s = stats();
    os << std::format(
        "[camel] gc: major={} requested={} deferred={} by_large_budget={} by_elder_exhausted={} "
        "by_elder_block_budget={} "
        "by_other={} alloc_young_mb={:.1f} alloc_elder_mb={:.1f} alloc_large_mb={:.1f} "
        "elder_overflow_objects={} elder_overflow_mb={:.1f} large_live_mb={:.1f} "
        "large_high_water_mb={:.1f} budget_mb={:.1f} growth_percent={} minor={} moved={} "
        "promoted={} elder_live_mb={:.1f} elder_reserved_mb={:.1f} elder_segments={} "
        "elder_block_live_mb={:.1f} elder_block_budget_mb={:.1f} "
        "minor_pause_ms={:.2f} minor_pause_max_ms={:.3f} major_pause_ms={:.2f} "
        "major_pause_max_ms={:.3f}\n",
        s.majorCollections,
        s.requestedCollections,
        s.deferredCollections,
        s.largeBudgetRequests,
        s.elderExhaustedRequests,
        s.elderBlockBudgetRequests,
        s.otherRequests,
        megabytes(s.youngBytesAllocated),
        megabytes(s.elderBytesAllocated),
        megabytes(s.largeBytesAllocated),
        s.elderOverflowAllocations,
        megabytes(s.elderOverflowBytes),
        megabytes(s.largeLiveBytes),
        megabytes(s.largeHighWaterMark),
        megabytes(s.largeBudgetBytes),
        s.largeGrowthPercent,
        s.minorCollections,
        s.movedObjects,
        s.promotedObjects,
        megabytes(s.elderLiveBytes),
        megabytes(s.elderReservedBytes),
        s.elderSegments,
        megabytes(s.elderBlockLiveBytes),
        megabytes(s.elderBlockBudgetBytes),
        static_cast<double>(s.minorPauseNs) / 1e6,
        static_cast<double>(s.minorPauseMaxNs) / 1e6,
        static_cast<double>(s.majorPauseNs) / 1e6,
        static_cast<double>(s.majorPauseMaxNs) / 1e6);
}

} // namespace camel::core::mm
