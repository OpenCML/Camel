/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: Mar. 07, 2026
 */

#include "camel/core/mm.h"
#include "camel/core/mm/alloc/header.h"
#include "nlohmann/json.hpp"

#include <cstdint>
#include <cstring>

namespace camel::core::mm {
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
        // 跳过已转发对象（逻辑上已迁出，由目标代表）
        if (hdr->forwarded())
            return;
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

// 按名称获取 Bump 区域指针（仅当前已启用的 region）
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
