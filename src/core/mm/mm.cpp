// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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


// --gc: what the command line chose before the allocator existed (-1: nothing).
std::atomic<int> selectedCollectorMode{-1};
std::atomic<bool> autoSpaceCreated{false};

// --gc-* and --space-segment-kb: the sizing and the checks the command line chose before the
// auto space existed. A number nobody set is 0, which the sizing reads as "the default".
AllocatorSizing selectedSizing{};
AllocatorChecks selectedChecks{};
std::atomic<bool> sizingSelected{false};
std::atomic<bool> checksSelected{false};

// --block-limit: what the command line chose before the block space existed (0: the default).
std::atomic<size_t> selectedBlockLimitBytes{0};
std::atomic<bool> blockSpaceCreated{false};

// std::profile gc_stats=<level>: the statistics asked for, kept until the allocator exists.
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

size_t largeObjectBudget() {
    const size_t megabytes = selectedSizing.largeBudgetMb;
    return (megabytes != 0 ? megabytes : 4) * 1024 * 1024;
}

// Growth of the large-object space between two collections, in percent of what the last sweep
// left (--gc-growth-percent).
size_t largeObjectGrowthPercent() {
    const size_t percent = selectedSizing.growthPercent;
    return percent != 0 ? percent : 100;
}

/// The collection a stress point performs, as `--gc-stress kind=...` names it.
GenerationalAllocatorWithGC::CollectionKind collectionKindOf(AllocatorChecks::Stress stress) {
    switch (stress) {
    case AllocatorChecks::Stress::Minor:
        return GenerationalAllocatorWithGC::CollectionKind::Minor;
    case AllocatorChecks::Stress::Both:
        return GenerationalAllocatorWithGC::CollectionKind::MinorAndMajor;
    case AllocatorChecks::Stress::Major:
    case AllocatorChecks::Stress::None:
        break;
    }
    return GenerationalAllocatorWithGC::CollectionKind::Major;
}
GenerationalAllocatorWithGC::DebugConfig debugConfig() {
    GenerationalAllocatorWithGC::DebugConfig config{};
    config.stressEveryNAllocations = selectedChecks.stressEveryAllocations;
    config.stressEveryNSafepoints  = selectedChecks.stressEverySafepoints;
    config.stressCollection        = collectionKindOf(selectedChecks.stress);
    config.logMovements            = selectedChecks.logMoves;
    config.poisonAtSafepoints      = selectedChecks.poison;
    config.verifyBeforeGC          = selectedChecks.verifyBefore;
    config.verifyAfterGC           = selectedChecks.verifyAfter;
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
    if (!selectedChecks.printConfig) {
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
    if (selectedChecks.poison) {
        return CollectorMode::Generational;
    }
    const int selected = selectedCollectorMode.load(std::memory_order_acquire);
    // Generational is the default (design principles 10.5: generational collection is the
    // default once usable); --gc names the other collector.
    return selected >= 0 ? static_cast<CollectorMode>(selected) : CollectorMode::Generational;
}

bool selectAllocatorSizing(const AllocatorSizing &sizing) {
    if (autoSpaceCreated.load(std::memory_order_acquire)) {
        return false;
    }
    selectedSizing = sizing;
    sizingSelected.store(true, std::memory_order_release);
    return true;
}

bool selectAllocatorChecks(const AllocatorChecks &checks) {
    if (autoSpaceCreated.load(std::memory_order_acquire)) {
        return false;
    }
    selectedChecks = checks;
    checksSelected.store(true, std::memory_order_release);
    return true;
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
        const auto debug = debugConfig();
        // Safepoint poisoning is a mode of the moving collector, so it turns it on.
        const bool youngCopying = collectorMode() == CollectorMode::Generational;
        autoSpaceCreated.store(true, std::memory_order_release);
        // The birth space (--gc-nursery-mb, default 4). Untouched pages cost nothing, so the
        // mark-sweep collector, which does not use it, does not pay for it.
        const size_t nurseryMb = selectedSizing.nurseryMb;
        const size_t nurseryBytes =
            std::clamp<size_t>(nurseryMb != 0 ? nurseryMb : 4, 1, 1024) * MB;
        // What counts as a large object (--gc-large-threshold-kb). Large objects get a
        // block of their own and never move. With mark-sweep the threshold is 4 KB. With the
        // generational collector it is 64 KB: an object born outside the birth space is old
        // from birth, and an array of a few thousand elements that dies young would keep
        // every young element it points at alive until the next major collection.
        const size_t thresholdKb = selectedSizing.largeThresholdKb;
        const size_t largeThreshold =
            thresholdKb != 0
                ? std::min(thresholdKb * KB, nurseryBytes / 8)
                : (youngCopying ? std::min<size_t>(64 * KB, nurseryBytes / 8) : 4 * KB);
        auto *instance = new GenerationalAllocatorWithGC(
            GenerationalAllocatorWithGC::Config{
                .birthSize = nurseryBytes,
                // Under safepoint poisoning nothing is promoted by age, so the survivor spaces
                // must hold whatever the birth space can.
                .havenSize    = debug.poisonAtSafepoints ? nurseryBytes : nurseryBytes / 4,
                .elderGenSize = 32 * MB,
                // Minor collections an object survives before it is promoted
                // (--gc-promotion-age, 1 to 15; 1 promotes at the first one, which is the
                // setting that shows missing write barriers).
                .promotionAgeThreshold = std::clamp<size_t>(
                    selectedSizing.promotionAge != 0 ? selectedSizing.promotionAge : 4, 1, 15),
                .largeObjThreshold = largeThreshold,
                // Large-object bytes between collections (--gc-large-budget-mb overrides).
                .largeObjCollectionBudget = largeObjectBudget(),
                .largeObjGrowthPercent    = largeObjectGrowthPercent(),
                .minorGCTriggerRatio      = 0.9f,
                .heapLimitBytes           = selectedSizing.heapLimitMb * MB,
                // Production remains address-stable by default; Phase 3 tests can opt into the
                // copying path explicitly to exercise remembered-set behavior.
                .enableYoungGenCopying = youngCopying});
        instance->configureDebug(debug);
        maybePrintDebugConfig(youngCopying, debug);
        // std::profile gc_stats=1: one line of pacing statistics on stderr at exit; =2: also
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

/// The limit of a space in bytes: what the command line chose (`--graph-space-limit-mb` and the
/// like), or the default when it chose nothing.
struct SpaceLimit {
    size_t fromOption;    // megabytes; 0 is the default
    const char *option;   // the option that sets it, for the line that names it
};

size_t spaceLimitMb(const SpaceLimit &limit, size_t defaultBytes) {
    return limit.fromOption != 0 ? limit.fromOption * MB : defaultBytes;
}

GrowableSpaceConfig spaceConfig(
    const char *name, const char *holds, size_t segmentBytes, size_t defaultLimitBytes,
    SpaceLimit limit) {
    GrowableSpaceConfig config;
    config.name       = name;
    config.holds      = holds;
    config.limitBytes = spaceLimitMb(limit, defaultLimitBytes);
    // --space-segment-kb sets the first segment of every growable space. It is a test
    // control: with small segments a small program makes the spaces grow, so growth can be
    // exercised under heap verification and collection stress.
    if (const size_t kilobytes = selectedSizing.spaceSegmentKb; kilobytes != 0) {
        segmentBytes = kilobytes * KB;
    }
    config.segmentBytes = std::min(segmentBytes, config.limitBytes);
    config.limitEnv     = limit.option;
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
        {selectedSizing.metaSpaceLimitMb, "--meta-space-limit-mb"}));
    return *allocator;
}

// Graph space: runtime graphs, released one by one by GCGraphManager.
GrowableFreeListAllocator &graphSpace() {
    static auto *allocator = new GrowableFreeListAllocator(spaceConfig(
        "graph",
        "runtime graphs",
        64 * MB,
        kDefaultGraphSpaceLimitBytes,
        {selectedSizing.graphSpaceLimitMb, "--graph-space-limit-mb"}));
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
            {0, "--block-limit"});
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
        {selectedSizing.permSpaceLimitMb, "--perm-space-limit-mb"}));
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
