/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 05, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/mm/foreign_handle.h"

#include "camel/core/rtdata/foreign.h"

#include <stdexcept>
#include <utility>

// The handle implementation is intentionally small: lifetime is still owned by the tracing GC, and
// handles only describe what native code may safely do with the pointed-to Camel object.

namespace camel::core::mm {

ForeignRootedHandle::ForeignRootedHandle(
    GenerationalAllocatorWithGC &allocator, rtdata::Object *object, const type::Type *type,
    std::string name)
    : root_(allocator, object, type, name.empty() ? "ForeignRootedHandle" : std::move(name)) {
    rtdata::noteForeignRootedHandleCreated();
}

ForeignRootedHandle::~ForeignRootedHandle() { rtdata::noteForeignRootedHandleDestroyed(); }

ForeignPinnedHandle::ForeignPinnedHandle(
    GenerationalAllocatorWithGC &allocator, rtdata::Object *object, const type::Type *type,
    std::string name)
    : root_(allocator, object, type, name.empty() ? "ForeignPinnedHandle" : std::move(name)) {
    if (object && !allocator.isObjectAddressStable(object)) {
        throw std::runtime_error(
            "foreign pinned handle requires an object in a non-moving allocation region");
    }
    pinnedAddress_ = object;
    rtdata::noteForeignPinnedHandleCreated();
}

ForeignPinnedHandle::~ForeignPinnedHandle() { rtdata::noteForeignPinnedHandleDestroyed(); }

namespace {

struct CountingResource {
    int value            = 0;
    int *disposeCount    = nullptr;
    int *moveNotifyCount = nullptr;
};

rtdata::ForeignResourceDescriptor selfTestDescriptor() {
    return rtdata::ForeignResourceDescriptor{
        .kind = "gc.foreign_resource.selftest",
        .deleter =
            [](void *resource) {
                auto *counter = static_cast<CountingResource *>(resource);
                if (counter->disposeCount) {
                    ++*counter->disposeCount;
                }
                delete counter;
            },
        .clone = {},
        .trace = {},
        .onMoved =
            [](void *resource) {
                auto *counter = static_cast<CountingResource *>(resource);
                if (counter->moveNotifyCount) {
                    ++*counter->moveNotifyCount;
                }
            },
        .containsCamelReferences = false,
        .finalizable             = true,
        .movable                 = true,
        .pinned                  = false,
    };
}

} // namespace

bool debugRunForeignResourceSelfTest(GenerationalAllocatorWithGC &allocator) {
    rtdata::resetForeignResourceStatsForTests();

    int disposeCount    = 0;
    int moveNotifyCount = 0;
    auto *resource      = new CountingResource{
        .value           = 7,
        .disposeCount    = &disposeCount,
        .moveNotifyCount = &moveNotifyCount,
    };
    rtdata::ForeignResourceObject *object =
        rtdata::ForeignResourceObject::create(allocator, selfTestDescriptor(), resource);

    {
        ForeignRootedHandle rooted(
            allocator,
            object,
            nullptr,
            "GC.foreign_resource.selftest.rooted");

        for (size_t i = 0; i < 8; ++i) {
            allocator.minorGC();
        }

        object = rooted.getAs<rtdata::ForeignResourceObject>();
        if (!object || object->resourceAs<CountingResource>()->value != 7) {
            throw std::runtime_error("foreign resource self-test lost the native payload");
        }
        if (disposeCount != 0) {
            throw std::runtime_error("foreign resource self-test disposed during wrapper move");
        }

        ForeignPinnedHandle pinned(
            allocator,
            object,
            nullptr,
            "GC.foreign_resource.selftest.pinned");
        void *stableAddress = pinned.address();
        allocator.majorGC();
        if (pinned.address() != stableAddress || pinned.get() != stableAddress) {
            throw std::runtime_error("foreign resource self-test moved a pinned object");
        }

        object = pinned.getAs<rtdata::ForeignResourceObject>();
        object->dispose();
        object->dispose();
        if (disposeCount != 1) {
            throw std::runtime_error("foreign resource self-test dispose was not idempotent");
        }

        bool accessFailed = false;
        try {
            (void)object->resource();
        } catch (const std::runtime_error &) {
            accessFailed = true;
        }
        if (!accessFailed) {
            throw std::runtime_error("foreign resource self-test allowed disposed access");
        }
    }

    allocator.majorGC();

    const rtdata::ForeignResourceStats stats = rtdata::foreignResourceStats();
    if (stats.createdControlBlocks != 1 || stats.disposedResources != 1 ||
        stats.finalizedWrappers != 1 || stats.releasedControlBlocks != 1 ||
        stats.liveControlBlocks != 0 || stats.activeRootedHandles != 0 ||
        stats.activePinnedHandles != 0 || stats.createdRootedHandles != 1 ||
        stats.createdPinnedHandles != 1) {
        throw std::runtime_error("foreign resource self-test stats did not match lifecycle");
    }

    return true;
}

} // namespace camel::core::mm
