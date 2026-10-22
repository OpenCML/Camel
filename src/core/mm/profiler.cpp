// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/mm.h"
#include "camel/core/mm/block.h"
#include "camel/core/mm/alloc/header.h"
#include "camel/core/rtdata/foreign.h"
#include "nlohmann/json.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>

namespace camel::core::mm {
namespace profiler {

using json = nlohmann::json;

// BumpPointer region snapshot.
static json bumpRegionToJson(const char *name, const BumpPointerAllocator &alloc, bool detailed) {
    auto *start     = alloc.start();
    auto *top       = alloc.top();
    auto *end       = alloc.end();
    size_t capacity = (end && start) ? static_cast<size_t>(end - start) : 0;
    size_t used     = (top && start) ? static_cast<size_t>(top - start) : 0;

    std::vector<json> objects;
    size_t objectBytes = 0;
    size_t objectCount = 0;
    alloc.iterateAllocated([&](ObjectHeader *hdr) {
        // Skip forwarded objects (logically moved out and represented by the target).
        if (hdr->forwarded())
            return;
        objectBytes += hdr->size();
        ++objectCount;
        if (detailed)
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
        {"objectCount", objectCount},
        {"objectBytes", objectBytes},
        {"objects", objects},
    };
}

// FreeList region snapshot (distinguish allocated blocks and free blocks).
static json freeListRegionToJson(const char *name, const FreeListAllocator &alloc, bool detailed) {
    auto *start      = alloc.start();
    auto *end        = alloc.end();
    size_t capacity  = (end && start) ? static_cast<size_t>(end - start) : 0;
    size_t available = alloc.available();
    size_t used      = capacity - available;

    std::vector<json> objects;
    size_t objectBytes = 0;
    size_t objectCount = 0;
    alloc.iterateAllocated([&](ObjectHeader *hdr) {
        objectBytes += hdr->size();
        ++objectCount;
        if (detailed)
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
        {"objectCount", objectCount},
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
    // The permanent space is a list of segments: "permSpace" is the first, "permSpace#<k>"
    // the k-th further one (the names snapshotToJson gives them).
    static constexpr const char kPermName[] = "permSpace";
    const size_t prefix                     = sizeof(kPermName) - 1;
    if (strncmp(name, kPermName, prefix) == 0) {
        size_t index = 0;
        if (name[prefix] == '#') {
            index = static_cast<size_t>(std::strtoull(name + prefix + 1, nullptr, 10));
        } else if (name[prefix] != '\0') {
            return nullptr;
        }
        const GrowableBumpAllocator &permSp = permSpace();
        return index < permSp.segmentCount() ? &permSp.segment(index) : nullptr;
    }
    return nullptr;
}

// Name of a segment of a growable space as a snapshot region.
static std::string segmentRegionName(const char *space, size_t index) {
    return index == 0 ? std::string(space) : std::string(space) + "#" + std::to_string(index);
}

// LargeObject region: no contiguous blocks, only an object list.
static json largeObjRegionToJson(const LargeObjectAllocator &alloc, bool detailed) {
    std::vector<json> objects;
    size_t objectBytes = 0;
    size_t objectCount = 0;
    alloc.iterateAllocated([&](ObjectHeader *hdr) {
        objectBytes += hdr->size();
        ++objectCount;
        if (detailed)
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
        {"objectCount", objectCount},
        {"objectBytes", objectBytes},
        {"objects", objects},
    };
}

static const char *collectionKindName(GenerationalAllocatorWithGC::CollectionKind kind) {
    switch (kind) {
    case GenerationalAllocatorWithGC::CollectionKind::None:
        return "none";
    case GenerationalAllocatorWithGC::CollectionKind::Minor:
        return "minor";
    case GenerationalAllocatorWithGC::CollectionKind::Major:
        return "major";
    case GenerationalAllocatorWithGC::CollectionKind::MinorAndMajor:
        return "minor+major";
    }
    return "unknown";
}

std::string configToJson() {
    auto &autoSp           = autoSpace();
    const auto debugConfig = autoSp.debugConfig();
    json root              = {
        {"youngCopying", autoSp.youngGenCopyingEnabled()},
        {"stress",
         {
             {"allocInterval", debugConfig.stressEveryNAllocations},
             {"safepointInterval", debugConfig.stressEveryNSafepoints},
             {"mode", collectionKindName(debugConfig.stressCollection)},
         }},
        {"verify",
         {
             {"before", debugConfig.verifyBeforeGC},
             {"after", debugConfig.verifyAfterGC},
         }},
        {"logMoves", debugConfig.logMovements},
    };
    return root.dump(2);
}

std::string snapshotToJson(bool detailed) {
    auto &autoSp = autoSpace();
    auto &metaSp = metaSpace();
    auto &permSp = permSpace();

    json regions = json::array();

    // Young Gen: birth, haven, cache.
    regions.push_back(bumpRegionToJson("birthSpace", autoSp.birthSpace(), detailed));
    regions.push_back(bumpRegionToJson("havenSpace", autoSp.havenSpace(), detailed));
    regions.push_back(bumpRegionToJson("cacheSpace", autoSp.cacheSpace(), detailed));

    // Elder Gen.
    for (size_t i = 0; i < autoSp.elderGenSpace().segmentCount(); ++i) {
        const std::string name = segmentRegionName("elderGenSpace", i);
        regions.push_back(
            freeListRegionToJson(name.c_str(), autoSp.elderGenSpace().segment(i), detailed));
    }

    // Large Object.
    regions.push_back(largeObjRegionToJson(autoSp.largeObjSpace(), detailed));

    // Meta Space.
    for (size_t i = 0; i < metaSp.segmentCount(); ++i) {
        const std::string name = segmentRegionName("metaSpace", i);
        regions.push_back(freeListRegionToJson(name.c_str(), metaSp.segment(i), detailed));
    }

    // Perm Space.
    for (size_t i = 0; i < permSp.segmentCount(); ++i) {
        const std::string name = segmentRegionName("permSpace", i);
        regions.push_back(bumpRegionToJson(name.c_str(), permSp.segment(i), detailed));
    }

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
             {"elderOverflowAllocations", stats.elderOverflowAllocations},
             {"elderBlockBudgetRequests", stats.elderBlockBudgetRequests},
             {"elderBlockLiveBytes", stats.elderBlockLiveBytes},
             {"elderBlockBudgetBytes", stats.elderBlockBudgetBytes},
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
        {"blocks",
         {
             {"liveBytes", camel::core::mm::blockBytesLive()},
             {"peakBytes", camel::core::mm::blockBytesPeak()},
             {"liveCount", camel::core::mm::blockLiveCount()},
             {"created", camel::core::mm::blockCreations()},
             {"spaceUsedBytes", blockSpace().usage().usedBytes},
             {"spaceReservedBytes", blockSpace().usage().reservedBytes},
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

std::string summaryToText() {
    auto &autoSp           = autoSpace();
    const auto stats       = autoSp.stats();
    const auto debugConfig = autoSp.debugConfig();
    const auto foreign     = camel::core::rtdata::foreignResourceStats();

    std::ostringstream os;
    os << "GC summary\n";
    os << "  mode: " << (autoSp.youngGenCopyingEnabled() ? "young-copying" : "non-moving") << "\n";
    os << "  stress: alloc=" << debugConfig.stressEveryNAllocations
       << " safepoint=" << debugConfig.stressEveryNSafepoints
       << " mode=" << collectionKindName(debugConfig.stressCollection) << "\n";
    os << "  verify: before=" << (debugConfig.verifyBeforeGC ? "true" : "false")
       << " after=" << (debugConfig.verifyAfterGC ? "true" : "false") << "\n";
    os << "  collections: minor=" << stats.minorCollections << " major=" << stats.majorCollections
       << " requested=" << stats.requestedCollections << " deferred=" << stats.deferredCollections
       << " elderOverflow=" << stats.elderOverflowAllocations << "\n";
    os << "  movement: moved=" << stats.movedObjects << " promoted=" << stats.promotedObjects
       << " rememberedSet=" << stats.rememberedSetSize << "\n";
    os << "  roots: sources=" << stats.rootSourceCount
       << " lastTracedRefs=" << stats.lastTracedRootReferenceCount << "\n";
    os << "  freed: elder=" << stats.freedElderObjects << " large=" << stats.freedLargeObjects
       << "\n";
    os << "  foreign: live=" << foreign.liveControlBlocks
       << " rooted=" << foreign.activeRootedHandles << " pinned=" << foreign.activePinnedHandles
       << " finalized=" << foreign.finalizedWrappers << "\n";
    os << "  blocks: live=" << camel::core::mm::blockLiveCount()
       << " bytes=" << camel::core::mm::blockBytesLive()
       << " peak=" << camel::core::mm::blockBytesPeak()
       << " created=" << camel::core::mm::blockCreations() << "\n";

    const auto sources = autoSp.rootSourceDescriptions();
    if (!sources.empty()) {
        os << "  rootSources:";
        for (const auto &source : sources) {
            os << " " << source;
        }
        os << "\n";
    }
    return os.str();
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
