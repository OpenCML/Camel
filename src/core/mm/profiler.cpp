/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: Feb. 23, 2026
 */

#include "camel/core/mm.h"
#include "camel/core/mm/alloc/header.h"
#include "nlohmann/json.hpp"

#include <cstdint>
#include <sstream>

namespace mm {
namespace profiler {

using json = nlohmann::json;

// BumpPointer 区域快照
static json bumpRegionToJson(const char *name, const BumpPointerAllocator &alloc) {
    auto *start     = alloc.start();
    auto *top       = alloc.top();
    auto *end       = alloc.end();
    size_t capacity = (end && start) ? static_cast<size_t>(end - start) : 0;
    size_t used     = (top && start) ? static_cast<size_t>(top - start) : 0;

    std::vector<json> objects;
    alloc.iterateAllocated([&](ObjectHeader *hdr) {
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
        {"objects", objects},
    };
}

// FreeList 区域快照（区分已分配块和空闲块）
static json freeListRegionToJson(const char *name, const FreeListAllocator &alloc) {
    auto *start      = alloc.start();
    auto *end        = alloc.end();
    size_t capacity  = (end && start) ? static_cast<size_t>(end - start) : 0;
    size_t available = alloc.available();
    size_t used      = capacity - available;

    std::vector<json> objects;
    alloc.iterateAllocated([&](ObjectHeader *hdr) {
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
        {"objects", objects},
    };
}

// LargeObject 区域：无连续块，仅对象列表
static json largeObjRegionToJson(const LargeObjectAllocator &alloc) {
    std::vector<json> objects;
    alloc.iterateAllocated([&](ObjectHeader *hdr) {
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
        {"objects", objects},
    };
}

std::string snapshotToJson() {
    auto &autoSp = autoSpace();
    auto &metaSp = metaSpace();
    auto &permSp = permSpace();

    json regions = json::array();

    // Young Gen: birth, haven, cache
    regions.push_back(bumpRegionToJson("birthSpace", autoSp.birthSpace()));
    regions.push_back(bumpRegionToJson("havenSpace", autoSp.havenSpace()));
    regions.push_back(bumpRegionToJson("cacheSpace", autoSp.cacheSpace()));

    // Elder Gen
    regions.push_back(freeListRegionToJson("elderGenSpace", autoSp.elderGenSpace()));

    // Large Object
    regions.push_back(largeObjRegionToJson(autoSp.largeObjSpace()));

    // Meta Space
    regions.push_back(freeListRegionToJson("metaSpace", metaSp));

    // Perm Space
    regions.push_back(bumpRegionToJson("permSpace", permSp));

    json root = {
        {"regions", regions},
        {"summary",
         {
             {"regionCount", regions.size()},
         }},
    };

    return root.dump(2);
}

} // namespace profiler
} // namespace mm
