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

#include <algorithm>
#include <format>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

// Debug configuration, heap verification, and diagnostic formatting for the
// generational GC. Keeping this separate from gen.cpp keeps the allocator hot path
// easier to inspect while preserving private invariant checks in one place.

namespace camel::core::mm {

void GenerationalAllocatorWithGC::configureDebug(DebugConfig config) {
    std::lock_guard<std::mutex> lock(mutex_);
    debugConfig_             = config;
    pendingStressCollection_ = CollectionKind::None;
}

GenerationalAllocatorWithGC::DebugConfig GenerationalAllocatorWithGC::debugConfig() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return debugConfig_;
}

GenerationalAllocatorWithGC::RuntimeStats GenerationalAllocatorWithGC::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    RuntimeStats result      = stats_;
    result.rememberedSetSize = rememberedSet_.size();
    result.rootSourceCount   = rootSourceCountUnlocked();
    return result;
}

std::vector<std::string> GenerationalAllocatorWithGC::rootSourceDescriptions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> sources;
    if (rootObjectSet_) {
        sources.push_back(std::format("runtimeGraphRoots({})", rootObjectSet_->size()));
    }
    for (const auto &entry : externalRootTracers_) {
        sources.push_back(entry.name);
    }
    return sources;
}

std::vector<GenerationalAllocatorWithGC::HeapVerificationIssue>
GenerationalAllocatorWithGC::verifyHeap() {
    std::lock_guard<std::mutex> lock(mutex_);
    return verifyHeapUnlocked();
}

void GenerationalAllocatorWithGC::verifyHeapOrThrow(std::string_view where) {
    std::lock_guard<std::mutex> lock(mutex_);
    throwIfVerificationFailed(verifyHeapUnlocked(), where);
}

size_t GenerationalAllocatorWithGC::rootSourceCountUnlocked() const {
    return (rootObjectSet_ ? 1 : 0) + externalRootTracers_.size();
}

std::string
GenerationalAllocatorWithGC::formatVerificationIssue(const HeapVerificationIssue &issue) {
    std::ostringstream os;
    os << issue.message;
    if (!issue.path.empty()) {
        os << " | path=" << issue.path;
    }
    if (!issue.owner.empty()) {
        os << " | owner=" << issue.owner;
    }
    if (!issue.slotType.empty()) {
        os << " | slotType=" << issue.slotType;
    }
    if (!issue.region.empty()) {
        os << " | region=" << issue.region;
    }
    if (issue.object != 0) {
        os << " | object=0x" << std::hex << issue.object << std::dec;
    }
    if (issue.target != 0) {
        os << " | target=0x" << std::hex << issue.target << std::dec;
    }
    return os.str();
}

GenerationalAllocatorWithGC::HeapVerificationIssue GenerationalAllocatorWithGC::makeIssue(
    std::string message, std::string path, std::string owner, std::string slotType,
    std::string region, uintptr_t object, uintptr_t target) {
    return HeapVerificationIssue{
        std::move(message),
        std::move(path),
        std::move(owner),
        std::move(slotType),
        std::move(region),
        object,
        target};
}

void GenerationalAllocatorWithGC::throwIfVerificationFailed(
    const std::vector<HeapVerificationIssue> &issues, std::string_view where) const {
    if (issues.empty()) {
        return;
    }

    std::ostringstream os;
    os << "GC heap verification failed";
    if (!where.empty()) {
        os << " " << where;
    }
    os << " with " << issues.size() << " issue(s):";
    const size_t limit = std::min<size_t>(issues.size(), 8);
    for (size_t i = 0; i < limit; ++i) {
        os << "\n  - " << formatVerificationIssue(issues[i]);
    }
    if (issues.size() > limit) {
        os << "\n  - ...";
    }
    throw std::runtime_error(os.str());
}

bool GenerationalAllocatorWithGC::autoRegionContainsPayload(
    const BumpPointerAllocator &allocator, rtdata::Object *object, std::string_view regionName,
    AllocRegion expectedRegion, std::vector<HeapVerificationIssue> &issues,
    std::string_view path) const {
    if (!allocator.contains(object)) {
        return false;
    }

    ObjectHeader *header = headerOf(object);
    if (!allocator.contains(header)) {
        issues.push_back(makeIssue(
            "Object payload points inside a bump region but its header is outside",
            std::string(path),
            {},
            {},
            std::string(regionName),
            0,
            reinterpret_cast<uintptr_t>(object)));
        return true;
    }
    if (!header->isValid()) {
        issues.push_back(makeIssue(
            "Invalid object header",
            std::string(path),
            {},
            {},
            std::string(regionName),
            reinterpret_cast<uintptr_t>(header),
            reinterpret_cast<uintptr_t>(object)));
        return true;
    }
    if (header->region() != expectedRegion) {
        issues.push_back(makeIssue(
            "Object header region tag does not match owning allocator",
            std::string(path),
            {},
            {},
            std::string(regionName),
            reinterpret_cast<uintptr_t>(header),
            reinterpret_cast<uintptr_t>(object)));
    }
    return true;
}

bool GenerationalAllocatorWithGC::autoRegionContainsPayload(
    const FreeListAllocator &allocator, rtdata::Object *object, std::string_view regionName,
    AllocRegion expectedRegion, std::vector<HeapVerificationIssue> &issues,
    std::string_view path) const {
    if (!allocator.contains(object)) {
        return false;
    }

    ObjectHeader *header = headerOf(object);
    if (!allocator.contains(header)) {
        issues.push_back(makeIssue(
            "Object payload points inside a free-list region but its header is outside",
            std::string(path),
            {},
            {},
            std::string(regionName),
            0,
            reinterpret_cast<uintptr_t>(object)));
        return true;
    }
    if (!header->isValid()) {
        issues.push_back(makeIssue(
            "Invalid object header",
            std::string(path),
            {},
            {},
            std::string(regionName),
            reinterpret_cast<uintptr_t>(header),
            reinterpret_cast<uintptr_t>(object)));
        return true;
    }
    if (header->region() != expectedRegion) {
        issues.push_back(makeIssue(
            "Object header region tag does not match owning allocator",
            std::string(path),
            {},
            {},
            std::string(regionName),
            reinterpret_cast<uintptr_t>(header),
            reinterpret_cast<uintptr_t>(object)));
    }
    return true;
}

bool GenerationalAllocatorWithGC::autoRegionContainsPayload(
    const LargeObjectAllocator &allocator, rtdata::Object *object,
    std::vector<HeapVerificationIssue> &issues, std::string_view path) const {
    if (!allocator.contains(object)) {
        return false;
    }

    ObjectHeader *header = headerOf(object);
    if (!header->isValid()) {
        issues.push_back(makeIssue(
            "Invalid large-object header",
            std::string(path),
            {},
            {},
            "auto.large",
            reinterpret_cast<uintptr_t>(header),
            reinterpret_cast<uintptr_t>(object)));
        return true;
    }
    if (header->region() != AllocRegion::LargeObj) {
        issues.push_back(makeIssue(
            "Large-object region tag does not match owning allocator",
            std::string(path),
            {},
            {},
            "auto.large",
            reinterpret_cast<uintptr_t>(header),
            reinterpret_cast<uintptr_t>(object)));
    }
    return true;
}

bool GenerationalAllocatorWithGC::knownObjectPointer(
    rtdata::Object *object, std::vector<HeapVerificationIssue> &issues,
    std::string_view path) const {
    if (!object) {
        return true;
    }
    if (autoRegionContainsPayload(
            birthSpace_,
            object,
            "auto.birth",
            AllocRegion::YoungGen,
            issues,
            path) ||
        autoRegionContainsPayload(
            havenSpace_,
            object,
            "auto.haven",
            AllocRegion::YoungGen,
            issues,
            path) ||
        autoRegionContainsPayload(
            cacheSpace_,
            object,
            "auto.cache",
            AllocRegion::YoungGen,
            issues,
            path) ||
        autoRegionContainsPayload(
            elderGenSpace_,
            object,
            "auto.elder",
            AllocRegion::ElderGen,
            issues,
            path) ||
        autoRegionContainsPayload(largeObjSpace_, object, issues, path)) {
        return true;
    }

    if (graphSpace().contains(object) || permSpace().contains(object)) {
        return true;
    }
    return false;
}

void GenerationalAllocatorWithGC::validateAllocatedHeaders(
    std::vector<HeapVerificationIssue> &issues, const BumpPointerAllocator &allocator,
    std::string_view regionName, AllocRegion expectedRegion) const {
    allocator.iterateAllocated([&](ObjectHeader *header) {
        if (!header->isValid()) {
            issues.push_back(makeIssue(
                "Invalid object header during region scan",
                {},
                {},
                {},
                std::string(regionName),
                reinterpret_cast<uintptr_t>(header)));
            return;
        }
        if (header->region() != expectedRegion) {
            issues.push_back(makeIssue(
                "Object header region tag does not match owning allocator",
                {},
                {},
                {},
                std::string(regionName),
                reinterpret_cast<uintptr_t>(header)));
        }
    });
}

void GenerationalAllocatorWithGC::validateAllocatedHeaders(
    std::vector<HeapVerificationIssue> &issues, const FreeListAllocator &allocator,
    std::string_view regionName, AllocRegion expectedRegion) const {
    allocator.iterateAllocated([&](ObjectHeader *header) {
        if (!header->isValid()) {
            issues.push_back(makeIssue(
                "Invalid object header during region scan",
                {},
                {},
                {},
                std::string(regionName),
                reinterpret_cast<uintptr_t>(header)));
            return;
        }
        if (header->region() != expectedRegion) {
            issues.push_back(makeIssue(
                "Object header region tag does not match owning allocator",
                {},
                {},
                {},
                std::string(regionName),
                reinterpret_cast<uintptr_t>(header)));
        }
    });
}

void GenerationalAllocatorWithGC::validateAllocatedHeaders(
    std::vector<HeapVerificationIssue> &issues, const LargeObjectAllocator &allocator) const {
    allocator.iterateAllocated([&](ObjectHeader *header) {
        if (!header->isValid()) {
            issues.push_back(makeIssue(
                "Invalid object header during large-object scan",
                {},
                {},
                {},
                "auto.large",
                reinterpret_cast<uintptr_t>(header)));
            return;
        }
        if (header->region() != AllocRegion::LargeObj) {
            issues.push_back(makeIssue(
                "Large-object region tag does not match owning allocator",
                {},
                {},
                {},
                "auto.large",
                reinterpret_cast<uintptr_t>(header)));
        }
    });
}

std::vector<GenerationalAllocatorWithGC::HeapVerificationIssue>
GenerationalAllocatorWithGC::verifyHeapUnlocked() {
    std::vector<HeapVerificationIssue> issues;

    if (!elderGenSpace_.validate()) {
        issues.push_back(
            makeIssue("Elder-generation free-list structure is invalid", {}, {}, {}, "auto.elder"));
    }

    validateAllocatedHeaders(issues, birthSpace_, "auto.birth", AllocRegion::YoungGen);
    validateAllocatedHeaders(issues, havenSpace_, "auto.haven", AllocRegion::YoungGen);
    validateAllocatedHeaders(issues, cacheSpace_, "auto.cache", AllocRegion::YoungGen);
    validateAllocatedHeaders(issues, elderGenSpace_, "auto.elder", AllocRegion::ElderGen);
    validateAllocatedHeaders(issues, largeObjSpace_);

    struct PendingTrace {
        rtdata::Object *object = nullptr;
        const type::Type *type = nullptr;
        std::string path;
    };

    std::vector<PendingTrace> stack;
    std::unordered_set<rtdata::Object *> visited;
    uint64_t tracedRootRefs = 0;

    auto enqueueIfTraceable =
        [&](rtdata::Object *object, const type::Type *objectType, std::string path) {
            if (!object || !visited.insert(object).second) {
                return;
            }
            stack.push_back(PendingTrace{object, objectType, std::move(path)});
        };

    auto validateRef = [&](rtdata::Object *ref,
                           const type::Type *refType,
                           const rtdata::RefTraceInfo &info,
                           std::string path) -> rtdata::Object * {
        ++tracedRootRefs;
        if (!ref) {
            return nullptr;
        }

        if (!knownObjectPointer(ref, issues, path)) {
            issues.push_back(makeIssue(
                "GC-traced slot points outside managed object regions",
                std::move(path),
                info.describe(),
                refType ? refType->toString() : std::string{"<unknown>"},
                {},
                0,
                reinterpret_cast<uintptr_t>(ref)));
            return ref;
        }

        enqueueIfTraceable(ref, refType, std::move(path));
        return ref;
    };

    if (rootObjectSet_) {
        for (size_t i = 0; i < rootObjectSet_->size(); ++i) {
            rtdata::Object *root = (*rootObjectSet_)[i];
            if (!root) {
                continue;
            }
            std::string path = std::format("runtimeGraphRoots[{}]", i);
            if (!knownObjectPointer(root, issues, path)) {
                issues.push_back(makeIssue(
                    "Root object points outside managed object regions",
                    path,
                    {},
                    {},
                    {},
                    0,
                    reinterpret_cast<uintptr_t>(root)));
                continue;
            }
            enqueueIfTraceable(root, nullptr, std::move(path));
        }
    }

    for (const auto &entry : externalRootTracers_) {
        entry.tracer(
            [&](rtdata::Object *ref,
                const type::Type *refType,
                const rtdata::RefTraceInfo &info) -> rtdata::Object * {
                std::string path = entry.name;
                std::string edge = info.describe();
                if (!edge.empty()) {
                    path += " -> ";
                    path += edge;
                }
                return validateRef(ref, refType, info, std::move(path));
            });
    }

    while (!stack.empty()) {
        PendingTrace current = std::move(stack.back());
        stack.pop_back();
        if (!current.object) {
            continue;
        }
        current.object->updateRefs(
            [&](rtdata::Object *ref,
                const type::Type *refType,
                const rtdata::RefTraceInfo &info) -> rtdata::Object * {
                std::string path = current.path;
                std::string edge = info.describe();
                if (!edge.empty()) {
                    path += " -> ";
                    path += edge;
                }
                return validateRef(ref, refType, info, std::move(path));
            },
            current.type);
    }

    stats_.lastTracedRootReferenceCount = tracedRootRefs;
    return issues;
}

} // namespace camel::core::mm
