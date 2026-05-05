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
 * Updated: May. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/mm/gen.h"

#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/string.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/utils/assert.h"
#include "camel/utils/brpred.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <mutex>
#include <new>
#include <stdexcept>
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

    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.writeBarriers;
    recordObjectTypeUnlocked(ownerObject, ownerType);
    recordObjectTypeUnlocked(storedObject, storedType);

    ObjectHeader *storedHeader = autoHeaderForPayload(storedObject);
    if (!inYoungGenSpace(storedHeader)) {
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

void GenerationalAllocatorWithGC::recordOldToYoungRef(void *oldObj, void *youngObj) {
    writeBarrier(
        static_cast<rtdata::Object *>(oldObj),
        nullptr,
        static_cast<rtdata::Object *>(youngObj),
        nullptr);
}

void GenerationalAllocatorWithGC::safepoint(std::string_view reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.safepoints;
    if (debugConfig_.stressEveryNSafepoints != 0 &&
        stats_.safepoints % debugConfig_.stressEveryNSafepoints == 0) {
        requestStressCollectionUnlocked("safepoint interval");
    }

    CollectionKind requested        = pendingSafepointCollection_;
    pendingSafepointCollection_     = CollectionKind::None;
    const std::string_view gcReason = reason.empty() ? std::string_view{"safepoint"} : reason;
    collectAtSafepointUnlocked(requested, gcReason);
    detail::autoSpaceSafepointSlowPath.store(
        debugConfig_.stressEveryNAllocations != 0 || debugConfig_.stressEveryNSafepoints != 0 ||
            pendingSafepointCollection_ != CollectionKind::None,
        std::memory_order_release);
}

void GenerationalAllocatorWithGC::minorGC() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.safepoints;
    requestCollectionAtSafepointUnlocked(CollectionKind::Minor, "manual minor GC");
    CollectionKind requested    = pendingSafepointCollection_;
    pendingSafepointCollection_ = CollectionKind::None;
    collectAtSafepointUnlocked(requested, "manual minor GC");
    detail::autoSpaceSafepointSlowPath.store(
        debugConfig_.stressEveryNAllocations != 0 || debugConfig_.stressEveryNSafepoints != 0 ||
            pendingSafepointCollection_ != CollectionKind::None,
        std::memory_order_release);
}

void GenerationalAllocatorWithGC::majorGC() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++stats_.safepoints;
    requestCollectionAtSafepointUnlocked(CollectionKind::Major, "manual major GC");
    CollectionKind requested    = pendingSafepointCollection_;
    pendingSafepointCollection_ = CollectionKind::None;
    collectAtSafepointUnlocked(requested, "manual major GC");
    detail::autoSpaceSafepointSlowPath.store(
        debugConfig_.stressEveryNAllocations != 0 || debugConfig_.stressEveryNSafepoints != 0 ||
            pendingSafepointCollection_ != CollectionKind::None,
        std::memory_order_release);
}

bool GenerationalAllocatorWithGC::debugRunRememberedSetSelfTest() {
    if (!enableYoungGenCopying_) {
        throw std::runtime_error(
            "GC remembered-set self-test requires CAMEL_GC_ENABLE_YOUNG_COPYING=1");
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
            requestCollectionAtSafepointUnlocked(
                CollectionKind::Major,
                "large object allocation failure");
            collectNonMovingUnlocked("large object allocation failure");
            ptr = largeObjSpace_.alloc(payloadSize, align);
            if (!ptr)
                throw std::bad_alloc();
        }
        auto *header = headerOf(ptr);
        header->setRegion(AllocRegion::LargeObj);
        objectTypes_.erase(header);
        return ptr;
    }

    if (!enableYoungGenCopying_) {
        void *ptr = elderGenSpace_.alloc(payloadSize, align);
        if (UNLIKELY(!ptr)) {
            requestCollectionAtSafepointUnlocked(CollectionKind::Major, "elder allocation failure");
            collectNonMovingUnlocked("elder allocation failure");
            ptr = elderGenSpace_.alloc(payloadSize, align);
            if (!ptr)
                throw std::bad_alloc();
        }

        auto *header = headerOf(ptr);
        header->setRegion(AllocRegion::ElderGen);
        objectTypes_.erase(header);
        return ptr;
    }

    // Try allocating in the birth space first.
    void *ptr = birthSpace_.alloc(payloadSize, align);
    if (UNLIKELY(!ptr)) {
        requestCollectionAtSafepointUnlocked(CollectionKind::Minor, "birth allocation failure");
        ptr = elderGenSpace_.alloc(payloadSize, align);
        if (UNLIKELY(!ptr)) {
            requestCollectionAtSafepointUnlocked(
                CollectionKind::Major,
                "birth allocation failure after elder fallback");
            collectNonMovingUnlocked("birth allocation failure after elder fallback");
            ptr = elderGenSpace_.alloc(payloadSize, align);
            if (!ptr) {
                throw std::bad_alloc();
            }
        }
        auto *header = headerOf(ptr);
        header->setRegion(AllocRegion::ElderGen);
        objectTypes_.erase(header);
        return ptr;
    }

    auto *header = headerOf(ptr);
    header->setRegion(AllocRegion::YoungGen);
    objectTypes_.erase(header);
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
    (void)reason;
    if (kind == CollectionKind::None) {
        return;
    }
    ++stats_.deferredCollections;
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

void GenerationalAllocatorWithGC::collectNonMovingUnlocked(std::string_view reason) {
    ++stats_.requestedCollections;
    ++stats_.allocationFailureCollections;

    if (debugConfig_.verifyBeforeGC) {
        throwIfVerificationFailed(verifyHeapUnlocked(), std::format("before {}", reason));
    }

    ++stats_.majorCollections;
    markPhase();
    sweepOldGen();
    sweepLargeObjects();

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
            if (!collectYoungReferenceEdges(oldObj, oldType).empty()) {
                retainedRememberedSet[oldHeader] = RememberedSetEntry{
                    .object = oldObj,
                    .type   = oldType,
                };
            }
        }
        rememberedSet_ = std::move(retainedRememberedSet);

        // 4. Scan copied/promoted objects with their static layouts.
        scanRelocatedObjects();
        for (const TraceEntry &entry : relocationScan_) {
            rememberOldObjectIfYoungRefsUnlocked(entry.object, entry.type);
        }

        // 5. Finalize dead young wrappers, then reset Birth and Cache. Forwarded source objects
        // still have a live copy in Haven/Elder, so finalizing only unforwarded payloads preserves
        // the "moving the wrapper does not retain/release the native resource" contract.
        finalizeUnforwardedObjectsIn(birthSpace_);
        finalizeUnforwardedObjectsIn(cacheSpace_);
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
    return objectType && objectType->isGCTraced() && objectType->code() != type::TypeCode::Ref &&
           objectType->code() != type::TypeCode::Any;
}

void GenerationalAllocatorWithGC::recordObjectTypeUnlocked(
    rtdata::Object *object, const type::Type *objectType) {
    if (!isPreciseGCType(objectType)) {
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
            if (ref && inYoungGenSpace(autoHeaderForPayload(ref))) {
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

void GenerationalAllocatorWithGC::rememberOldObjectIfYoungRefsUnlocked(
    rtdata::Object *object, const type::Type *objectType) {
    ObjectHeader *header = autoHeaderForPayload(object);
    if (!header || (!inElderGenSpace(header) && !inLargeObjSpace(header))) {
        return;
    }
    recordObjectTypeUnlocked(object, objectType);
    if (!collectYoungReferenceEdges(object, objectType).empty()) {
        rememberedSet_[header] = RememberedSetEntry{
            .object = object,
            .type   = objectType,
        };
    }
}

void GenerationalAllocatorWithGC::finalizeUnforwardedObjectsIn(BumpPointerAllocator &allocator) {
    std::vector<ObjectHeader *> deadObjects;
    allocator.iterateAllocated([&](ObjectHeader *header) {
        objectTypes_.erase(header);
        if (!header->forwarded()) {
            deadObjects.push_back(header);
        }
    });
    finalizeObjects(deadObjects);
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

    elderGenSpace_.iterateAllocated([&unreachable](ObjectHeader *header) {
        if (!header->marked_) {
            unreachable.push_back(header);
        }
    });

    stats_.freedElderObjects += unreachable.size();
    finalizeObjects(unreachable);
    for (ObjectHeader *header : unreachable) {
        rememberedSet_.erase(header);
        objectTypes_.erase(header);
    }
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
    finalizeObjects(unreachable);
    for (ObjectHeader *header : unreachable) {
        rememberedSet_.erase(header);
        objectTypes_.erase(header);
    }
    largeObjSpace_.freeBulk(unreachable);
}

} // namespace camel::core::mm
