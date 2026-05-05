/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: May. 05, 2026
 */

#include "camel/core/mm.h"
#include "camel/core/mm/alloc/header.h"
#include "camel/core/rtdata/foreign.h"
#include "nlohmann/json.hpp"

#include <cstdint>
#include <cstring>

namespace camel::core::mm {
namespace profiler {

using json = nlohmann::json;

// BumpPointer region snapshot.
static json bumpRegionToJson(const char *name, const BumpPointerAllocator &alloc) {
    auto *start     = alloc.start();
    auto *top       = alloc.top();
    auto *end       = alloc.end();
    size_t capacity = (end && start) ? static_cast<size_t>(end - start) : 0;
    size_t used     = (top && start) ? static_cast<size_t>(top - start) : 0;

    std::vector<json> objects;
    size_t objectBytes = 0;
    alloc.iterateAllocated([&](ObjectHeader *hdr) {
        // Skip forwarded objects (logically moved out and represented by the target).
        if (hdr->forwarded())
            return;
        objectBytes += hdr->size();
        objects.push_back({
            {"addr", reinterpret_cast<uintptr_t>(hdr)},
            {"size", hdr->size()},
            {"age", static_cast<int>(hdr->age())},
            {"region", static_cast<int>(hdr->region())},
        });
    });

    return {
        {"name", name},
        {"type", "bump"},
        {"start", reinterpret_cast<uintptr_t>(start)},
        {"top", reinterpret_cast<uintptr_t>(top)},
        {"end", reinterpret_cast<uintptr_t>(end)},
        {"capacity", capacity},
        {"used", used},
        {"available", alloc.available()},
        {"objectCount", objects.size()},
        {"objectBytes", objectBytes},
        {"objects", objects},
    };
}

// FreeList region snapshot (distinguish allocated blocks and free blocks).
static json freeListRegionToJson(const char *name, const FreeListAllocator &alloc) {
    auto *start      = alloc.start();
    auto *end        = alloc.end();
    size_t capacity  = (end && start) ? static_cast<size_t>(end - start) : 0;
    size_t available = alloc.available();
    size_t used      = capacity - available;

    std::vector<json> objects;
    size_t objectBytes = 0;
    alloc.iterateAllocated([&](ObjectHeader *hdr) {
        objectBytes += hdr->size();
        objects.push_back({
            {"addr", reinterpret_cast<uintptr_t>(hdr)},
            {"size", hdr->size()},
            {"age", static_cast<int>(hdr->age())},
            {"region", static_cast<int>(hdr->region())},
        });
    });

    return {
        {"name", name},
        {"type", "freelist"},
        {"start", reinterpret_cast<uintptr_t>(start)},
        {"end", reinterpret_cast<uintptr_t>(end)},
        {"capacity", capacity},
        {"used", used},
        {"available", available},
        {"objectCount", objects.size()},
        {"objectBytes", objectBytes},
        {"objects", objects},
    };
}

// Look up a Bump region by name (only currently enabled regions).
static const BumpPointerAllocator *getBumpRegionByName(const char *name) {
    auto &autoSp = autoSpace();
    if (strcmp(name, "birthSpace") == 0)
        return &autoSp.birthSpace();
    if (strcmp(name, "havenSpace") == 0)
        return &autoSp.havenSpace();
    if (strcmp(name, "cacheSpace") == 0)
        return &autoSp.cacheSpace();
    BumpPointerAllocator &permSp = permSpace();
    if (strcmp(name, "permSpace") == 0)
        return &permSp;
    return nullptr;
}

// LargeObject region: no contiguous blocks, only an object list.
static json largeObjRegionToJson(const LargeObjectAllocator &alloc) {
    std::vector<json> objects;
    size_t objectBytes = 0;
    alloc.iterateAllocated([&](ObjectHeader *hdr) {
        objectBytes += hdr->size();
        objects.push_back({
            {"addr", reinterpret_cast<uintptr_t>(hdr)},
            {"size", hdr->size()},
            {"age", static_cast<int>(hdr->age())},
            {"region", static_cast<int>(hdr->region())},
        });
    });

    return {
        {"name", "largeObj"},
        {"type", "largeobj"},
        {"objectCount", objects.size()},
        {"objectBytes", objectBytes},
        {"objects", objects},
    };
}

std::string snapshotToJson() {
    auto &autoSp = autoSpace();
    auto &metaSp = metaSpace();
    auto &permSp = permSpace();

    json regions = json::array();

    // Young Gen: birth, haven, cache.
    regions.push_back(bumpRegionToJson("birthSpace", autoSp.birthSpace()));
    regions.push_back(bumpRegionToJson("havenSpace", autoSp.havenSpace()));
    regions.push_back(bumpRegionToJson("cacheSpace", autoSp.cacheSpace()));

    // Elder Gen.
    regions.push_back(freeListRegionToJson("elderGenSpace", autoSp.elderGenSpace()));

    // Large Object.
    regions.push_back(largeObjRegionToJson(autoSp.largeObjSpace()));

    // Meta Space.
    regions.push_back(freeListRegionToJson("metaSpace", metaSp));

    // Perm Space.
    regions.push_back(bumpRegionToJson("permSpace", permSp));

    size_t totalObjectCount = 0;
    size_t totalObjectBytes = 0;
    for (const auto &region : regions) {
        totalObjectCount += region.value("objectCount", 0);
        totalObjectBytes += region.value("objectBytes", 0);
    }

    const auto stats        = autoSp.stats();
    const auto foreignStats = camel::core::rtdata::foreignResourceStats();

    json root = {
        {"regions", regions},
        {"gc",
         {
             {"allocations", stats.allocations},
             {"safepoints", stats.safepoints},
             {"deferredCollections", stats.deferredCollections},
             {"requestedCollections", stats.requestedCollections},
             {"allocationFailureCollections", stats.allocationFailureCollections},
             {"writeBarriers", stats.writeBarriers},
             {"minorCollections", stats.minorCollections},
             {"majorCollections", stats.majorCollections},
             {"movedObjects", stats.movedObjects},
             {"promotedObjects", stats.promotedObjects},
             {"freedElderObjects", stats.freedElderObjects},
             {"freedLargeObjects", stats.freedLargeObjects},
             {"rootSourceCount", stats.rootSourceCount},
             {"lastTracedRootReferenceCount", stats.lastTracedRootReferenceCount},
             {"rememberedSetSize", stats.rememberedSetSize},
             {"rootSources", autoSp.rootSourceDescriptions()},
         }},
        {"foreignResources",
         {
             {"createdControlBlocks", foreignStats.createdControlBlocks},
             {"disposedResources", foreignStats.disposedResources},
             {"finalizedWrappers", foreignStats.finalizedWrappers},
             {"releasedControlBlocks", foreignStats.releasedControlBlocks},
             {"liveControlBlocks", foreignStats.liveControlBlocks},
             {"createdRootedHandles", foreignStats.createdRootedHandles},
             {"createdPinnedHandles", foreignStats.createdPinnedHandles},
             {"activeRootedHandles", foreignStats.activeRootedHandles},
             {"activePinnedHandles", foreignStats.activePinnedHandles},
         }},
        {"summary",
         {
             {"regionCount", regions.size()},
             {"objectCount", totalObjectCount},
             {"objectBytes", totalObjectBytes},
         }},
    };

    return root.dump(2);
}

std::string regionMemoryRawToJson(const char *regionName, size_t offset, size_t limit) {
    const size_t kMaxLimit = 4096;
    if (limit == 0 || limit > kMaxLimit)
        limit = kMaxLimit;

    const BumpPointerAllocator *alloc = getBumpRegionByName(regionName);
    if (!alloc) {
        return json{{"error", "unknown or unsupported region"}}.dump();
    }

    const std::byte *start = alloc->start();
    const std::byte *top   = alloc->top();
    const std::byte *end   = alloc->end();
    size_t used            = (start && top) ? static_cast<size_t>(top - start) : 0;
    size_t capacity        = (start && end) ? static_cast<size_t>(end - start) : 0;

    if (offset >= capacity) {
        return json{
            {"region", regionName},
            {"offset", offset},
            {"limit", 0},
            {"used", used},
            {"capacity", capacity},
            {"data", json::array()},
            {"hasMore", false},
        }
            .dump();
    }

    size_t avail       = (offset + limit > capacity) ? (capacity - offset) : limit;
    json data          = json::array();
    const std::byte *p = start + offset;
    for (size_t i = 0; i < avail; ++i)
        data.push_back(static_cast<uint8_t>(p[i]));

    return json{
        {"region", regionName},
        {"offset", offset},
        {"limit", avail},
        {"used", used},
        {"capacity", capacity},
        {"data", data},
        {"hasMore", offset + avail < capacity},
    }
        .dump();
}

std::string regionObjectsToJson(const char *regionName, size_t offset, size_t limit) {
    const size_t kMaxLimit = 200;
    if (limit == 0 || limit > kMaxLimit)
        limit = kMaxLimit;

    const BumpPointerAllocator *alloc = getBumpRegionByName(regionName);
    if (!alloc) {
        return json{
            {"error", "unknown or unsupported region"},
            {"objects", json::array()},
            {"total", 0},
            {"hasMore", false}}
            .dump();
    }

    std::vector<json> objects;
    size_t total   = 0;
    size_t skipped = 0;
    alloc->iterateAllocated([&](ObjectHeader *hdr) {
        total++;
        if (skipped < offset) {
            skipped++;
            return;
        }
        if (objects.size() < limit) {
            objects.push_back({
                {"addr", reinterpret_cast<uintptr_t>(hdr)},
                {"size", hdr->size()},
                {"age", static_cast<int>(hdr->age())},
                {"region", static_cast<int>(hdr->region())},
            });
        }
    });

    const uintptr_t regionStart =
        (alloc->start() != nullptr) ? reinterpret_cast<uintptr_t>(alloc->start()) : 0;
    return json{
        {"region", regionName},
        {"regionStart", regionStart},
        {"objects", objects},
        {"total", total},
        {"hasMore", offset + objects.size() < total},
    }
        .dump();
}

} // namespace profiler
} // namespace camel::core::mm
