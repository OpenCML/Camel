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
 * Created: Dec. 10, 2025
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/mm.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

namespace camel::core::mm {

namespace detail {
std::atomic_bool autoSpaceSafepointSlowPath{false};
std::atomic<uint32_t> nativeCodeDepth{0};
} // namespace detail

namespace {

std::string lowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string envString(const char *name) {
#ifdef _WIN32
    char *envValue = nullptr;
    size_t size  = 0;
    if (_dupenv_s(&envValue, &size, name) != 0 || envValue == nullptr) {
        return {};
    }
    std::string value(envValue, size > 0 ? size - 1 : 0);
    std::free(envValue);
    return value;
#else
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string{};
#endif
}

bool envFlag(const char *name) {
    std::string value = envString(name);
    if (value.empty()) {
        return false;
    }
    value = lowerAscii(std::move(value));
    return value != "0" && value != "false" && value != "off" && value != "no";
}

size_t envSize(const char *name) {
    std::string raw = envString(name);
    if (raw.empty()) {
        return 0;
    }
    char *end                = nullptr;
    unsigned long long value = std::strtoull(raw.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<size_t>(value) : 0;
}

GenerationalAllocatorWithGC::CollectionKind envCollectionKind() {
    std::string value = envString("CAMEL_GC_STRESS_MODE");
    if (value.empty()) {
        return GenerationalAllocatorWithGC::CollectionKind::Major;
    }
    value = lowerAscii(std::move(value));
    if (value == "minor") {
        return GenerationalAllocatorWithGC::CollectionKind::Minor;
    }
    if (value == "both" || value == "minor+major" || value == "all") {
        return GenerationalAllocatorWithGC::CollectionKind::MinorAndMajor;
    }
    return GenerationalAllocatorWithGC::CollectionKind::Major;
}

// --gc: what the command line chose before the allocator existed (-1: nothing).
std::atomic<int> selectedCollectorMode{-1};
std::atomic<bool> autoSpaceCreated{false};

// --block-limit: what the command line chose before the block space existed (0: the default).
std::atomic<size_t> selectedBlockLimitBytes{0};
std::atomic<bool> blockSpaceCreated{false};

// std::profile(gc_stats=<level>): the statistics asked for, kept until the allocator exists.
std::atomic<int> requestedStatsLevel{0};
std::once_flag statsAtExitOnce;

// The allocator whose statistics are printed at exit.
GenerationalAllocatorWithGC *statsInstance = nullptr;

void installStatsAtExit() {
    std::call_once(statsAtExitOnce, [] {
        std::atexit([] {
            if (statsInstance != nullptr) {
                statsInstance->reportStats(std::cerr);
            }
            reportSpaceUsage(std::cerr);
        });
    });
}

size_t largeObjectBudgetFromEnv() {
    const size_t megabytes = envSize("CAMEL_GC_LARGE_BUDGET_MB");
    return (megabytes != 0 ? megabytes : 4) * 1024 * 1024;
}

// Growth of the large-object space between two collections, in percent of what the last sweep
// left (CAMEL_GC_GROWTH_PERCENT).
size_t largeObjectGrowthPercentFromEnv() {
    const size_t percent = envSize("CAMEL_GC_GROWTH_PERCENT");
    return percent != 0 ? percent : 100;
}

GenerationalAllocatorWithGC::DebugConfig debugConfigFromEnv() {
    GenerationalAllocatorWithGC::DebugConfig config{};
    config.stressEveryNAllocations = envSize("CAMEL_GC_STRESS_ALLOC");
    config.stressEveryNSafepoints  = envSize("CAMEL_GC_STRESS_SAFEPOINT");
    config.stressCollection        = envCollectionKind();
    config.logMovements            = envFlag("CAMEL_GC_LOG_MOVES");
    config.poisonAtSafepoints      = envFlag("CAMEL_GC_POISON");

    std::string verifyRaw = envString("CAMEL_GC_VERIFY");
    if (!verifyRaw.empty()) {
        std::string value = lowerAscii(std::move(verifyRaw));
        if (value == "before" || value == "pre") {
            config.verifyBeforeGC = true;
        } else if (value == "after" || value == "post") {
            config.verifyAfterGC = true;
        } else if (value != "0" && value != "false" && value != "off" && value != "no") {
            config.verifyBeforeGC = true;
            config.verifyAfterGC  = true;
        }
    }
    return config;
}

const char *collectionKindName(GenerationalAllocatorWithGC::CollectionKind kind) {
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

void maybePrintDebugConfig(
    bool youngCopying, const GenerationalAllocatorWithGC::DebugConfig &config) {
    if (!envFlag("CAMEL_GC_PRINT_CONFIG")) {
        return;
    }
    std::cerr << "[camel] GC config:" << " youngCopying=" << (youngCopying ? "true" : "false")
              << " verifyBefore=" << (config.verifyBeforeGC ? "true" : "false")
              << " verifyAfter=" << (config.verifyAfterGC ? "true" : "false")
              << " stressAlloc=" << config.stressEveryNAllocations
              << " stressSafepoint=" << config.stressEveryNSafepoints
              << " stressMode=" << collectionKindName(config.stressCollection)
              << " logMoves=" << (config.logMovements ? "true" : "false")
              << " poison=" << (config.poisonAtSafepoints ? "true" : "false") << '\n';
}

} // namespace

const char *collectorModeName(CollectorMode mode) {
    return mode == CollectorMode::Generational ? "generational" : "mark-sweep";
}

std::optional<CollectorMode> parseCollectorMode(std::string_view text) {
    if (text == "generational" || text == "gen" || text == "1") {
        return CollectorMode::Generational;
    }
    if (text == "mark-sweep" || text == "marksweep" || text == "0") {
        return CollectorMode::MarkSweep;
    }
    return std::nullopt;
}

bool selectCollectorMode(CollectorMode mode) {
    if (autoSpaceCreated.load(std::memory_order_acquire)) {
        return false;
    }
    selectedCollectorMode.store(static_cast<int>(mode), std::memory_order_release);
    return true;
}

CollectorMode collectorMode() {
    if (autoSpaceCreated.load(std::memory_order_acquire)) {
        return autoSpace().youngGenCopyingEnabled() ? CollectorMode::Generational
                                                    : CollectorMode::MarkSweep;
    }
    // Safepoint poisoning is a mode of the moving collector, so it turns it on.
    if (envFlag("CAMEL_GC_POISON")) {
        return CollectorMode::Generational;
    }
    const int selected = selectedCollectorMode.load(std::memory_order_acquire);
    // Generational is the default (design principles 10.5: generational collection is the
    // default once usable); --gc names the other collector.
    return selected >= 0 ? static_cast<CollectorMode>(selected) : CollectorMode::Generational;
}

bool selectBlockSpaceLimit(size_t bytes) {
    if (blockSpaceCreated.load(std::memory_order_acquire)) {
        return false;
    }
    selectedBlockLimitBytes.store(bytes, std::memory_order_release);
    return true;
}

void enableStatsReport(int level) {
    if (level <= 0) {
        return;
    }
    requestedStatsLevel.store(level, std::memory_order_release);
    if (autoSpaceCreated.load(std::memory_order_acquire)) {
        autoSpace().setStatsLevel(level);
        statsInstance = &autoSpace();
        installStatsAtExit();
    }
}

bool statsReportEnabled() { return requestedStatsLevel.load(std::memory_order_acquire) != 0; }

// Managed automatically by the GC system and must remain reachable from roots.
GenerationalAllocatorWithGC &autoSpace() {
    static auto *allocator = [] {
        const auto debugConfig = debugConfigFromEnv();
        // Safepoint poisoning is a mode of the moving collector, so it turns it on.
        const bool youngCopying = collectorMode() == CollectorMode::Generational;
        autoSpaceCreated.store(true, std::memory_order_release);
        // The birth space (CAMEL_GC_NURSERY_MB, default 4). Untouched pages cost nothing, so
        // the mark-sweep collector, which does not use it, does not pay for it.
        const size_t nurseryMb = envSize("CAMEL_GC_NURSERY_MB");
        const size_t nurseryBytes =
            std::clamp<size_t>(nurseryMb != 0 ? nurseryMb : 4, 1, 1024) * MB;
        // What counts as a large object (CAMEL_GC_LARGE_THRESHOLD_KB). Large objects get a
        // block of their own and never move. With mark-sweep the threshold is 4 KB. With the
        // generational collector it is 64 KB: an object born outside the birth space is old
        // from birth, and an array of a few thousand elements that dies young would keep
        // every young element it points at alive until the next major collection.
        const size_t thresholdKb = envSize("CAMEL_GC_LARGE_THRESHOLD_KB");
        const size_t largeThreshold =
            thresholdKb != 0
                ? std::min(thresholdKb * KB, nurseryBytes / 8)
                : (youngCopying ? std::min<size_t>(64 * KB, nurseryBytes / 8) : 4 * KB);
        auto *instance = new GenerationalAllocatorWithGC(
            GenerationalAllocatorWithGC::Config{
                .birthSize = nurseryBytes,
                // Under safepoint poisoning nothing is promoted by age, so the survivor spaces
                // must hold whatever the birth space can.
                .havenSize    = debugConfig.poisonAtSafepoints ? nurseryBytes : nurseryBytes / 4,
                .elderGenSize = 32 * MB,
                // Minor collections an object survives before it is promoted
                // (CAMEL_GC_PROMOTION_AGE, 1 to 15; 1 promotes at the first one, which is the
                // setting that shows missing write barriers).
                .promotionAgeThreshold = std::clamp<size_t>(
                    envSize("CAMEL_GC_PROMOTION_AGE") != 0 ? envSize("CAMEL_GC_PROMOTION_AGE") : 4,
                    1,
                    15),
                .largeObjThreshold = largeThreshold,
                // Large-object bytes between collections (CAMEL_GC_LARGE_BUDGET_MB overrides).
                .largeObjCollectionBudget = largeObjectBudgetFromEnv(),
                .largeObjGrowthPercent    = largeObjectGrowthPercentFromEnv(),
                .minorGCTriggerRatio      = 0.9f,
                .heapLimitBytes           = envSize("CAMEL_GC_HEAP_LIMIT_MB") * MB,
                // Production remains address-stable by default; Phase 3 tests can opt into the
                // copying path explicitly to exercise remembered-set behavior.
                .enableYoungGenCopying = youngCopying});
        instance->configureDebug(debugConfig);
        maybePrintDebugConfig(youngCopying, debugConfig);
        // std::profile(gc_stats=1): one line of pacing statistics on stderr at exit; =2: also
        // one line per major collection. The request is kept until the allocator exists, since
        // the pass may run before the heap is first used.
        if (const int level = requestedStatsLevel.load(std::memory_order_acquire); level != 0) {
            instance->setStatsLevel(level);
            statsInstance = instance;
            installStatsAtExit();
        }
        return instance;
    }();
    return *allocator;
}

// The three spaces below hold what the program is made of, so they grow with it: segment by
// segment, up to a limit that only guards against a compiler that never stops allocating (see
// alloc/growable.h). The first segment of each is as large as the whole space used to be, so a
// program that fitted before sees one segment and the same addresses-never-move contract.

namespace {

/// The limit of a space in bytes: `<env>=<megabytes>` when set to a positive number.
size_t spaceLimitFromEnv(const char *env, size_t defaultBytes) {
    const size_t megabytes = envSize(env);
    return megabytes != 0 ? megabytes * MB : defaultBytes;
}

GrowableSpaceConfig spaceConfig(
    const char *name, const char *holds, size_t segmentBytes, size_t defaultLimitBytes,
    const char *limitEnv) {
    GrowableSpaceConfig config;
    config.name       = name;
    config.holds      = holds;
    config.limitBytes = spaceLimitFromEnv(limitEnv, defaultLimitBytes);
    // CAMEL_SPACE_SEGMENT_KB=<kilobytes> sets the first segment of every growable space. It
    // is a test control: with small segments a small program makes the spaces grow, so growth
    // can be exercised under heap verification and collection stress.
    if (const size_t kilobytes = envSize("CAMEL_SPACE_SEGMENT_KB"); kilobytes != 0) {
        segmentBytes = kilobytes * KB;
    }
    config.segmentBytes = std::min(segmentBytes, config.limitBytes);
    config.limitEnv     = limitEnv;
    return config;
}

void printSpaceUsage(std::ostream &os, const char *name, const SpaceUsage &usage) {
    os << ' ' << name << "_used_kb=" << usage.usedBytes / KB << ' ' << name
       << "_peak_kb=" << usage.peakUsedBytes / KB << ' ' << name
       << "_reserved_mb=" << usage.reservedBytes / MB << ' ' << name
       << "_segments=" << usage.segments << ' ' << name << "_allocs=" << usage.allocations << ' '
       << name << "_limit_mb=" << usage.limitBytes / MB;
}

} // namespace

// Metadata space: manually managed allocation and release (compile-time graph arenas).
GrowableFreeListAllocator &metaSpace() {
    // The process-level meta space uses an intentionally-leaked singleton to avoid static
    // destruction order problems: some global objects may still call metaSpace().free() during
    // shutdown.
    static auto *allocator = new GrowableFreeListAllocator(spaceConfig(
        "meta",
        "constants and function objects of the graphs being compiled",
        16 * MB,
        kDefaultMetaSpaceLimitBytes,
        kMetaSpaceLimitEnv));
    return *allocator;
}

// Graph space: runtime graphs, released one by one by GCGraphManager.
GrowableFreeListAllocator &graphSpace() {
    static auto *allocator = new GrowableFreeListAllocator(spaceConfig(
        "graph",
        "runtime graphs",
        64 * MB,
        kDefaultGraphSpaceLimitBytes,
        kGraphSpaceLimitEnv));
    return *allocator;
}

// Data blocks: a free-list space, so a block is released as soon as its last owner
// value is gone. It is not the collected heap: data does not move and a pointer handed out
// stays valid. Freeing reads the size from the block header, so the space keeps no per-block
// table: blocks are freed in bulk under memory pressure and the table would only grow.
GrowableFreeListAllocator &blockSpace() {
    static auto *allocator = new GrowableFreeListAllocator(([] {
        // The block space holds data, not program text: one run that builds several models in one
        // process legitimately needs hundreds of megabytes, and the space gives memory back as soon
        // as the last owner goes, so its limit only has to catch a runaway allocation. Its default
        // is therefore of the same order as the collected heap, not the 256 MB of the program
        // spaces; --block-limit raises it before the space exists.
        const size_t selected = selectedBlockLimitBytes.load(std::memory_order_acquire);
        GrowableSpaceConfig config = spaceConfig(
            "block",
            "data blocks",
            8 * MB,
            selected != 0 ? selected : kDefaultBlockSpaceLimitBytes,
            "--block-limit");
        config.sizesFromHeaders = true;
        blockSpaceCreated.store(true, std::memory_order_release);
        return config;
    })());
    return *allocator;
}

// Permanent space: allocate only, never free (types and layouts).
GrowableBumpAllocator &permSpace() {
    static auto *allocator = new GrowableBumpAllocator(spaceConfig(
        "permanent",
        "types and layouts",
        32 * MB,
        kDefaultPermSpaceLimitBytes,
        kPermSpaceLimitEnv));
    return *allocator;
}

void reportSpaceUsage(std::ostream &os) {
    os << "[camel] spaces:";
    printSpaceUsage(os, "graph", graphSpace().usage());
    printSpaceUsage(os, "perm", permSpace().usage());
    printSpaceUsage(os, "meta", metaSpace().usage());
    os << '\n';
}

} // namespace camel::core::mm
