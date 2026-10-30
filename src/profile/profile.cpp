// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The profile session behind camel/profile/profile.h: the counters of a run, the per-operator
 * timing thunks (camel/profile/operators.h), the per-round numbers of every loop that ran, and
 * the two reports.
 *
 * The recording is off by default and costs one out-of-line call where a back end asks. When it
 * is on, an operator call costs a pair of clock reads (the report prints what pair cost) and two
 * relaxed atomic adds; a loop round costs a handful of relaxed atomic loads.
 */

#include "camel/profile/profile.h"

#include "camel/core/operator.h"
#include "camel/core/rtdata/base.h"
#include "camel/profile/allocation.h"
#include "camel/profile/operators.h"
#include "camel/utils/path.h"
#include "camel/utils/phase_timer.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <format>
#include <iostream>
#include <map>
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::profile {

namespace detail {
std::atomic<bool> enabledFlag{false};
} // namespace detail

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point gOrigin = Clock::now();

uint64_t nowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - gOrigin).count());
}

std::thread::id gThread;

/// The timed operator whose call is running on this thread, or kNoOperator. The timing thunk
/// sets it around the operator call; the allocator hooks read it to attribute an allocation to
/// the call that caused it. A thread-local rather than a global, because a scheduler runs
/// operators on its workers too and two workers are inside different operators at once.
constexpr size_t kNoOperator    = static_cast<size_t>(-1);
thread_local size_t tlsOperator = kNoOperator;

/// Sets tlsOperator for the duration of one operator call, and restores the previous one (an
/// operator that itself calls a timed operator): the innermost call owns the allocation, so no
/// allocation is counted in two rows.
struct OperatorCallScope {
    explicit OperatorCallScope(size_t index) noexcept : previous_(tlsOperator) {
        tlsOperator = index;
    }
    ~OperatorCallScope() noexcept { tlsOperator = previous_; }
    OperatorCallScope(const OperatorCallScope &)            = delete;
    OperatorCallScope &operator=(const OperatorCallScope &) = delete;
    size_t previous_;
};

/// The counters a step boundary reads: what the run has done so far. The allocation counters
/// are the part inside operator calls, which is what the step's operator rows attribute.
struct Totals {
    uint64_t operators = 0;
    uint64_t kernels   = 0;
    uint64_t toDevice  = 0; // bytes
    uint64_t toHost    = 0; // bytes
    uint64_t syncs     = 0;
    uint64_t allocCalls = 0;
    uint64_t allocBlocks = 0;
    uint64_t allocBytes  = 0;
    uint64_t allocNs     = 0;
    /// The fusion groups' part: how many times a group ran and how many operator executions it
    /// stood for (its members plus its tail). Read with `operators`, never in its place.
    uint64_t fusionRuns      = 0;
    uint64_t fusionOperators = 0;
};

std::atomic<uint64_t> gOperators{0};
std::atomic<uint64_t> gOperatorNs{0};
std::atomic<uint64_t> gKernels{0};
std::atomic<uint64_t> gKernelNs{0};
std::atomic<uint64_t> gToDeviceBytes{0};
std::atomic<uint64_t> gToHostBytes{0};
std::atomic<uint64_t> gTransfers{0};
std::atomic<uint64_t> gSyncs{0};
std::atomic<uint64_t> gSyncNs{0};
std::atomic<uint64_t> gDeviceCompiles{0};
std::atomic<uint64_t> gDeviceCompileNs{0};
std::atomic<uint64_t> gUntimedCalls{0};

/// The fusion groups the run executed (camel/execute/fusion_group.h): one entry per (key, value),
/// with the operators of its members in execution order. `kMaxFusionGroups` is a bound like the
/// operator table's; a program with more groups says so in the report instead of hiding them.
constexpr size_t kMaxFusionGroups = 32;
struct FusionGroupStat {
    std::string key;
    std::string name;
    std::vector<std::string> members;
    std::atomic<uint64_t> runs{0};
    std::atomic<uint64_t> ns{0};
    std::atomic<uint64_t> firstNs{0};
};
std::array<FusionGroupStat, kMaxFusionGroups> gFusionGroups;
std::atomic<size_t> gFusionCount{0};
std::atomic<uint64_t> gFusionRuns{0};
std::atomic<uint64_t> gFusionOperators{0};
std::atomic<uint64_t> gFusionNs{0};
std::atomic<uint64_t> gFusionOverflowGroups{0};
std::mutex gFusionMutex;

/// The index of a group in `gFusionGroups`, adding it on first sight. `kMaxFusionGroups` when the
/// table is full. The table is written under `gFusionMutex`; the counters are atomics.
size_t fusionGroupIndex(
    std::string_view key, std::string_view name, std::span<const std::string_view> members) {
    std::lock_guard lock(gFusionMutex);
    const size_t count = gFusionCount.load(std::memory_order_relaxed);
    for (size_t i = 0; i < count; ++i) {
        if (std::string_view(gFusionGroups[i].key) == key &&
            std::string_view(gFusionGroups[i].name) == name) {
            return i;
        }
    }
    if (count == kMaxFusionGroups) {
        gFusionOverflowGroups.fetch_add(1, std::memory_order_relaxed);
        return kMaxFusionGroups;
    }
    FusionGroupStat &entry = gFusionGroups[count];
    entry.key              = std::string(key);
    entry.name             = std::string(name);
    entry.members.reserve(members.size());
    for (const std::string_view member : members) {
        entry.members.emplace_back(member);
    }
    gFusionCount.store(count + 1, std::memory_order_release);
    return count;
}

/// The allocator's own numbers, all calls the Camel value allocators served while recording:
/// `gAllocCalls` counts every one, `gAllocBlocks` the ones that were a data block's payload and
/// `gAllocBytes` the payload bytes those blocks asked for. `inside` is the part made during an
/// operator call (what the operator rows show); the rest happens before or between calls.
std::atomic<uint64_t> gAllocCalls{0};
std::atomic<uint64_t> gAllocBlocks{0};
std::atomic<uint64_t> gAllocBytes{0};
std::atomic<uint64_t> gAllocNs{0};
std::atomic<uint64_t> gAllocCallsInside{0};
std::atomic<uint64_t> gAllocBlocksInside{0};
std::atomic<uint64_t> gAllocBytesInside{0};
std::atomic<uint64_t> gAllocNsInside{0};

std::mutex gMutex;
std::string gBackend;
std::string gDevice = "cpu";
std::vector<HostLoopHint> gHints;
std::unordered_map<uint64_t, std::string> gLoopPositions;
ReportOptions gOptions;
std::string gJsonPath;

/// The kernel-timing table (`std::profile opstats=true`). Read by a module's kernel dispatch, so
/// it is an atomic the pass sets once before anything runs; the session itself is not started.
std::atomic<bool> gKernelTiming{false};

/// In-place updates, as the execution plan decided them and as the array answered at run time
/// (docs/design/arrays-memory/README.cn.md 4.3.1): `gUpdatePlannedReuse` and
/// `gUpdatePlannedByCount` are the marks of every plan built during the run, and
/// `gArrayCountInPlace` / `gArrayCountCopied` the answers of the update operators that asked the
/// array itself. The first pair is what the plan decided, the second what happened.
std::atomic<uint64_t> gUpdatePlannedReuse{0};
std::atomic<uint64_t> gUpdatePlannedByCount{0};
std::atomic<uint64_t> gUpdatePlannedCopy{0};
std::atomic<uint64_t> gArrayCountInPlace{0};
std::atomic<uint64_t> gArrayCountCopied{0};

/// One record per tee, by the number a report prints for it, in the order the run first heard of
/// each tee. A tee is pulled on whichever thread reads it -- a scheduler may read two of them at
/// once -- and a record is updated once per element, so the maps are behind their own mutex rather
/// than the report's. What tells two tees apart is the stream value each reads, which arrives
/// unchanged on every pull of that tee.
std::map<const void *, uint64_t> gStreamTeeNumbers;
std::map<uint64_t, StreamTee> gStreamTees;
uint64_t gStreamTeeCount = 0;
std::mutex gStreamTeeMutex;

Totals totalsNow() {
    Totals totals;
    totals.operators = gOperators.load(std::memory_order_relaxed);
    totals.kernels   = gKernels.load(std::memory_order_relaxed);
    totals.toDevice  = gToDeviceBytes.load(std::memory_order_relaxed);
    totals.toHost    = gToHostBytes.load(std::memory_order_relaxed);
    totals.syncs     = gSyncs.load(std::memory_order_relaxed);
    totals.allocCalls  = gAllocCallsInside.load(std::memory_order_relaxed);
    totals.allocBlocks = gAllocBlocksInside.load(std::memory_order_relaxed);
    totals.allocBytes  = gAllocBytesInside.load(std::memory_order_relaxed);
    totals.allocNs     = gAllocNsInside.load(std::memory_order_relaxed);
    totals.fusionRuns      = gFusionRuns.load(std::memory_order_relaxed);
    totals.fusionOperators = gFusionOperators.load(std::memory_order_relaxed);
    return totals;
}

/// One loop of the program, as the rounds report it.
struct LoopStat {
    uint64_t origin = 0;
    uint64_t rounds = 0;
    /// The totals at the previous round boundary, and when it was.
    Totals atLastBoundary{};
    uint64_t lastBoundaryNs = 0;
    uint64_t firstBoundaryNs = 0;
    /// Per round, over the rounds after the first (the first boundary has no previous one):
    /// the sum, the smallest and the largest of each.
    uint64_t samples = 0;
    uint64_t opsSum = 0, opsMin = UINT64_MAX, opsMax = 0;
    uint64_t kernelsSum = 0, kernelsMin = UINT64_MAX, kernelsMax = 0;
    uint64_t toDeviceSum = 0, toDeviceMax = 0;
    uint64_t toHostSum = 0, toHostMax = 0;
    uint64_t syncsSum = 0, syncsMax = 0;
    uint64_t allocCallsSum = 0, allocCallsMax = 0;
    uint64_t allocBlocksSum = 0, allocBlocksMax = 0;
    uint64_t allocBytesSum = 0, allocBytesMax = 0;
    uint64_t allocNsSum = 0, allocNsMax = 0;
    uint64_t fusionRunsSum = 0;
    uint64_t fusionOpsSum = 0;
    uint64_t wallSumNs = 0, wallMinNs = UINT64_MAX, wallMaxNs = 0;
    /// Every round's wall time, up to kMaxRoundSamples: the median of them is what a step costs
    /// on a machine that is doing something else too.
    std::vector<uint64_t> walls;

    static constexpr size_t kMaxRoundSamples = 8192;

    uint64_t medianWallNs() const {
        if (walls.empty()) {
            return 0;
        }
        std::vector<uint64_t> sorted = walls;
        std::sort(sorted.begin(), sorted.end());
        return sorted[sorted.size() / 2];
    }

    void takeRound(const Totals &now, uint64_t boundaryNs) {
        ++rounds;
        if (rounds > 1) {
            const Totals &last = atLastBoundary;
            const uint64_t ops = now.operators - last.operators;
            const uint64_t kernels = now.kernels - last.kernels;
            const uint64_t toDevice = now.toDevice - last.toDevice;
            const uint64_t toHost = now.toHost - last.toHost;
            const uint64_t syncs = now.syncs - last.syncs;
            const uint64_t allocCalls  = now.allocCalls - last.allocCalls;
            const uint64_t allocBlocks = now.allocBlocks - last.allocBlocks;
            const uint64_t allocBytes  = now.allocBytes - last.allocBytes;
            const uint64_t allocNs     = now.allocNs - last.allocNs;
            const uint64_t fusionRuns  = now.fusionRuns - last.fusionRuns;
            const uint64_t fusionOps   = now.fusionOperators - last.fusionOperators;
            // The round lasted from the previous boundary to this one.
            const uint64_t wallNs = boundaryNs > lastBoundaryNs ? boundaryNs - lastBoundaryNs : 0;
            ++samples;
            opsSum += ops;
            opsMin  = std::min(opsMin, ops);
            opsMax  = std::max(opsMax, ops);
            kernelsSum += kernels;
            kernelsMin  = std::min(kernelsMin, kernels);
            kernelsMax  = std::max(kernelsMax, kernels);
            toDeviceSum += toDevice;
            toDeviceMax  = std::max(toDeviceMax, toDevice);
            toHostSum += toHost;
            toHostMax  = std::max(toHostMax, toHost);
            syncsSum += syncs;
            syncsMax  = std::max(syncsMax, syncs);
            allocCallsSum += allocCalls;
            allocCallsMax  = std::max(allocCallsMax, allocCalls);
            allocBlocksSum += allocBlocks;
            allocBlocksMax  = std::max(allocBlocksMax, allocBlocks);
            allocBytesSum += allocBytes;
            allocBytesMax  = std::max(allocBytesMax, allocBytes);
            allocNsSum += allocNs;
            allocNsMax  = std::max(allocNsMax, allocNs);
            fusionRunsSum += fusionRuns;
            fusionOpsSum += fusionOps;
            wallSumNs += wallNs;
            wallMinNs = std::min(wallMinNs, wallNs);
            wallMaxNs = std::max(wallMaxNs, wallNs);
            if (walls.size() < kMaxRoundSamples) {
                walls.push_back(wallNs);
            }
        }
        atLastBoundary = now;
        lastBoundaryNs = boundaryNs;
        if (firstBoundaryNs == 0) {
            firstBoundaryNs = boundaryNs;
        }
    }
};

std::vector<LoopStat> gLoops;
std::unordered_map<uint64_t, size_t> gLoopIndex;
/// The loop of the previous round: one comparison instead of a hash lookup per round. An index
/// rather than a pointer, so that growing the vector cannot leave it dangling.
size_t gLastLoopIndex   = 0;
uint64_t gLastLoopOrigin = 0;
bool gHasLastLoop        = false;

/// The measured cost of one timestamp pair, filled at the first report.
double gOverheadNs = -1.0;

void measureOverhead() {
    if (gOverheadNs >= 0.0) {
        return;
    }
    // The smallest mean of several batches: an estimate of the timestamp pair itself, which a
    // batch interrupted by anything (a page fault, a context switch) would inflate.
    constexpr size_t kBatches = 40;
    constexpr size_t kPairs   = 500;
    double best               = 1e18;
    volatile uint64_t sink    = 0;
    for (size_t batch = 0; batch < kBatches; ++batch) {
        const auto start = Clock::now();
        for (size_t i = 0; i < kPairs; ++i) {
            const auto a = Clock::now();
            const auto b = Clock::now();
            sink += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
        }
        const auto total =
            std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        best = std::min(best, static_cast<double>(total) / static_cast<double>(kPairs));
    }
    (void)sink;
    gOverheadNs = best;
}

// ---------------------------------------------------------------------------
// The timing thunks (camel/profile/operators.h)
// ---------------------------------------------------------------------------

struct TimedOperator {
    std::string uri;
    operator_t op = nullptr;
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> ns{0};
    /// What the first call cost: it pays the one-time setup of the operator (a thread pool, a
    /// lazily compiled kernel), which the mean of the rest does not dilute.
    std::atomic<uint64_t> firstNs{0};
    /// Of the calls' wall time, what was inside the Camel allocators: the counters the new row
    /// reports. `allocBytes` is the payload bytes of the block allocations (a block space call
    /// also rounds up and adds a header, which is the allocator's business, not the value's).
    std::atomic<uint64_t> allocCalls{0};
    std::atomic<uint64_t> allocNs{0};
    std::atomic<uint64_t> allocBlocks{0};
    std::atomic<uint64_t> allocBytes{0};
};

std::array<TimedOperator, kMaxTimedOperators> gTimed;
size_t gTimedCount = 0;
std::unordered_map<std::string, size_t> gTimedIndex;

uint64_t nsBetween(Clock::time_point from) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - from).count());
}

template <size_t I>
slot_t timedThunk(
    ArgsView &with, ArgsView &norm, camel::core::context::Context &context) {
    TimedOperator &entry = gTimed[I];
    const auto start     = Clock::now();
    const OperatorCallScope call(I);
    slot_t result        = entry.op(with, norm, context);
    const uint64_t ns    = nsBetween(start);
    if (entry.calls.fetch_add(1, std::memory_order_relaxed) == 0) {
        entry.firstNs.store(ns, std::memory_order_relaxed);
    }
    entry.ns.fetch_add(ns, std::memory_order_relaxed);
    gOperatorNs.fetch_add(ns, std::memory_order_relaxed);
    gOperators.fetch_add(1, std::memory_order_relaxed);
    return result;
}

template <size_t... Is>
constexpr std::array<operator_t, sizeof...(Is)> makeThunks(std::index_sequence<Is...>) {
    return std::array<operator_t, sizeof...(Is)>{&timedThunk<Is>...};
}

const std::array<operator_t, kMaxTimedOperators> gThunks =
    makeThunks(std::make_index_sequence<kMaxTimedOperators>{});

std::string formatDuration(uint64_t ns) {
    if (ns >= 1'000'000'000ull) {
        return std::format("{:.2f} s", static_cast<double>(ns) / 1e9);
    }
    if (ns >= 1'000'000ull) {
        return std::format("{:.2f} ms", static_cast<double>(ns) / 1e6);
    }
    if (ns >= 1'000ull) {
        return std::format("{:.1f} µs", static_cast<double>(ns) / 1e3);
    }
    return std::format("{} ns", ns);
}

std::string formatBytes(uint64_t bytes) {
    if (bytes >= 1024ull * 1024ull * 1024ull) {
        return std::format("{:.2f} GiB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
    }
    if (bytes >= 1024ull * 1024ull) {
        return std::format("{:.2f} MiB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    if (bytes >= 1024ull) {
        return std::format("{:.1f} KiB", static_cast<double>(bytes) / 1024.0);
    }
    return std::format("{} B", bytes);
}

/// The loop the program's steps are: the one that carries the most operators per round (a
/// training loop carries the whole step; a loop inside it carries less), the one that ran the
/// most rounds when two are equal where it counts.
const LoopStat *stepLoop() {
    const LoopStat *chosen = nullptr;
    double best            = -1.0;
    for (const LoopStat &loop : gLoops) {
        if (loop.samples == 0) {
            continue;
        }
        const double perRound =
            static_cast<double>(loop.opsSum) / static_cast<double>(loop.samples);
        if (perRound > best || (perRound == best && chosen != nullptr && loop.rounds > chosen->rounds)) {
            best   = perRound;
            chosen = &loop;
        }
    }
    return chosen;
}

std::string percentOf(uint64_t part, uint64_t whole) {
    if (whole == 0) {
        return "0.0%";
    }
    return std::format("{:.1f}%", 100.0 * static_cast<double>(part) / static_cast<double>(whole));
}

struct Row {
    std::string uri;
    std::string device;
    uint64_t calls  = 0;
    uint64_t ns     = 0;
    uint64_t firstNs = 0;
    uint64_t allocCalls = 0;
    uint64_t allocNs    = 0;
    uint64_t allocBlocks = 0;
    uint64_t allocBytes  = 0;
};

/// The device an operator ran on. An operator is CPU code: it runs on the host even when it
/// drives a device (a `tiles:` operator uploads, launches and waits). The column says which of
/// the two paths the call belongs to, which is what the report is for.
std::string deviceOf(const std::string &uri);

std::vector<Row> operatorRows() {
    std::vector<Row> rows;
    rows.reserve(gTimedCount);
    for (size_t i = 0; i < gTimedCount; ++i) {
        const TimedOperator &entry = gTimed[i];
        const uint64_t calls       = entry.calls.load(std::memory_order_relaxed);
        if (calls == 0) {
            continue;
        }
        rows.push_back(Row{
            entry.uri,
            deviceOf(entry.uri),
            calls,
            entry.ns.load(std::memory_order_relaxed),
            entry.firstNs.load(std::memory_order_relaxed),
            entry.allocCalls.load(std::memory_order_relaxed),
            entry.allocNs.load(std::memory_order_relaxed),
            entry.allocBlocks.load(std::memory_order_relaxed),
            entry.allocBytes.load(std::memory_order_relaxed)});
    }
    std::sort(rows.begin(), rows.end(), [](const Row &a, const Row &b) {
        if (a.ns != b.ns) {
            return a.ns > b.ns;
        }
        return a.calls > b.calls;
    });
    return rows;
}

} // namespace

// ---------------------------------------------------------------------------
// The session
// ---------------------------------------------------------------------------

bool enabled() noexcept { return detail::enabledFlag.load(std::memory_order_relaxed); }

void enable() {
    if (detail::enabledFlag.load(std::memory_order_relaxed)) {
        return;
    }
    gOrigin  = Clock::now();
    gThread  = std::this_thread::get_id();
    detail::enabledFlag.store(true, std::memory_order_relaxed);
}

void reset() {
    gOperators.store(0);
    gOperatorNs.store(0);
    gKernels.store(0);
    gKernelNs.store(0);
    gToDeviceBytes.store(0);
    gToHostBytes.store(0);
    gTransfers.store(0);
    gSyncs.store(0);
    gSyncNs.store(0);
    gDeviceCompiles.store(0);
    gDeviceCompileNs.store(0);
    gUntimedCalls.store(0);
    gUpdatePlannedReuse.store(0);
    gUpdatePlannedByCount.store(0);
    gUpdatePlannedCopy.store(0);
    gArrayCountInPlace.store(0);
    gArrayCountCopied.store(0);
    gFusionRuns.store(0);
    gFusionOperators.store(0);
    gFusionNs.store(0);
    gFusionOverflowGroups.store(0);
    {
        std::lock_guard lock(gFusionMutex);
        for (size_t i = 0; i < gFusionCount.load(std::memory_order_relaxed); ++i) {
            FusionGroupStat &entry = gFusionGroups[i];
            entry.key.clear();
            entry.name.clear();
            entry.members.clear();
            entry.runs.store(0, std::memory_order_relaxed);
            entry.ns.store(0, std::memory_order_relaxed);
            entry.firstNs.store(0, std::memory_order_relaxed);
        }
        gFusionCount.store(0, std::memory_order_release);
    }
    gAllocCalls.store(0);
    gAllocBlocks.store(0);
    gAllocBytes.store(0);
    gAllocNs.store(0);
    gAllocCallsInside.store(0);
    gAllocBlocksInside.store(0);
    gAllocBytesInside.store(0);
    gAllocNsInside.store(0);
    for (size_t i = 0; i < gTimedCount; ++i) {
        gTimed[i].calls.store(0);
        gTimed[i].ns.store(0);
        gTimed[i].allocCalls.store(0);
        gTimed[i].allocNs.store(0);
        gTimed[i].allocBlocks.store(0);
        gTimed[i].allocBytes.store(0);
    }
    gLoops.clear();
    gLoopIndex.clear();
    gHasLastLoop    = false;
    gLastLoopIndex  = 0;
    gLastLoopOrigin = 0;
    {
        std::lock_guard lock(gMutex);
        gHints.clear();
        gLoopPositions.clear();
        gBackend.clear();
        gDevice = "cpu";
        gOptions = ReportOptions{};
        gJsonPath.clear();
    }
    {
        std::lock_guard lock(gStreamTeeMutex);
        gStreamTeeNumbers.clear();
        gStreamTees.clear();
        gStreamTeeCount = 0;
    }
    gOrigin = Clock::now();
}

void planUpdateMarks(size_t reuseInput, size_t byArrayCount, size_t copy) noexcept {
    gUpdatePlannedReuse.fetch_add(reuseInput, std::memory_order_relaxed);
    gUpdatePlannedByCount.fetch_add(byArrayCount, std::memory_order_relaxed);
    gUpdatePlannedCopy.fetch_add(copy, std::memory_order_relaxed);
}

void arrayCountAnswer(bool inPlace) noexcept {
    if (inPlace) {
        gArrayCountInPlace.fetch_add(1, std::memory_order_relaxed);
    } else {
        gArrayCountCopied.fetch_add(1, std::memory_order_relaxed);
    }
}

void streamTee(const void *stream, const StreamTee &record) {
    std::lock_guard lock(gStreamTeeMutex);
    // The first pull of a tee is the run's first word of it, and gives it its number; every later
    // pull of the same stream value joins that record.
    uint64_t &number = gStreamTeeNumbers[stream];
    if (number == 0) {
        number = ++gStreamTeeCount;
    }
    StreamTee &stored = gStreamTees[number];
    // What a tee is given and what it is does not change while it runs: the hint, the cap and the
    // element type are written once and repeated. The two counts are running totals, so what is
    // kept is the widest each has been.
    stored.hint      = record.hint;
    stored.cap       = record.cap;
    stored.elem      = record.elem;
    stored.elemBytes = record.elemBytes;
    stored.elements  = std::max(stored.elements, record.elements);
    stored.peak      = std::max(stored.peak, record.peak);
}

void loopRound(uint64_t origin) noexcept {
    if (std::this_thread::get_id() != gThread) {
        return;
    }
    size_t index = 0;
    if (gHasLastLoop && gLastLoopOrigin == origin) {
        index = gLastLoopIndex;
    } else {
        auto it = gLoopIndex.find(origin);
        if (it == gLoopIndex.end()) {
            index = gLoops.size();
            gLoops.push_back(LoopStat{.origin = origin});
            gLoopIndex.emplace(origin, index);
        } else {
            index = it->second;
        }
        gLastLoopIndex  = index;
        gLastLoopOrigin = origin;
        gHasLastLoop    = true;
    }
    gLoops[index].takeRound(totalsNow(), nowNs());
}

void fusionGroup(
    std::string_view key, std::string_view name, std::span<const std::string_view> members,
    uint64_t ns) {
    const size_t index = fusionGroupIndex(key, name, members);
    gFusionRuns.fetch_add(1, std::memory_order_relaxed);
    gFusionOperators.fetch_add(members.size(), std::memory_order_relaxed);
    gFusionNs.fetch_add(ns, std::memory_order_relaxed);
    if (index == kMaxFusionGroups) {
        return;
    }
    FusionGroupStat &entry = gFusionGroups[index];
    if (entry.runs.fetch_add(1, std::memory_order_relaxed) == 0) {
        entry.firstNs.store(ns, std::memory_order_relaxed);
    }
    entry.ns.fetch_add(ns, std::memory_order_relaxed);
}

void kernelLaunch(std::string_view label, uint64_t ns) {
    (void)label;
    gKernels.fetch_add(1, std::memory_order_relaxed);
    gKernelNs.fetch_add(ns, std::memory_order_relaxed);
}

void transferToDevice(uint64_t bytes) {
    gTransfers.fetch_add(1, std::memory_order_relaxed);
    gToDeviceBytes.fetch_add(bytes, std::memory_order_relaxed);
}

void transferToHost(uint64_t bytes) {
    gTransfers.fetch_add(1, std::memory_order_relaxed);
    gToHostBytes.fetch_add(bytes, std::memory_order_relaxed);
}

void synchronise(std::string_view what) {
    (void)what;
    gSyncs.fetch_add(1, std::memory_order_relaxed);
}

void deviceCompile(std::string_view what, uint64_t ns) {
    (void)what;
    gDeviceCompiles.fetch_add(1, std::memory_order_relaxed);
    gDeviceCompileNs.fetch_add(ns, std::memory_order_relaxed);
}

void setBackend(std::string backend) {
    std::lock_guard lock(gMutex);
    gBackend = std::move(backend);
}

void setDevice(std::string device) {
    std::lock_guard lock(gMutex);
    gDevice = std::move(device);
}

void addHostLoopHint(HostLoopHint hint) {
    std::lock_guard lock(gMutex);
    gHints.push_back(std::move(hint));
}

void noteLoopPosition(uint64_t origin, std::string position) {
    if (origin == 0 || position.empty()) {
        return;
    }
    std::lock_guard lock(gMutex);
    gLoopPositions[origin] = std::move(position);
}

void setReportOptions(const ReportOptions &options) {
    std::lock_guard lock(gMutex);
    gOptions = options;
}

ReportOptions reportOptions() {
    std::lock_guard lock(gMutex);
    return gOptions;
}

void setJsonPath(std::string path) {
    std::lock_guard lock(gMutex);
    gJsonPath = std::move(path);
}

std::string jsonPath() {
    std::lock_guard lock(gMutex);
    return gJsonPath;
}

void setKernelTiming(bool on) { gKernelTiming.store(on, std::memory_order_relaxed); }

bool kernelTiming() noexcept { return gKernelTiming.load(std::memory_order_relaxed); }

double measurementOverheadNs() {
    measureOverhead();
    return gOverheadNs;
}

void noteAllocation(uint64_t ns, uint64_t bytes, bool block) noexcept {
    // The scope already checked the flag; a session that stopped between the two reads (the
    // report is written while nothing runs) must not record into it.
    if (!detail::enabledFlag.load(std::memory_order_relaxed)) {
        return;
    }
    gAllocCalls.fetch_add(1, std::memory_order_relaxed);
    gAllocNs.fetch_add(ns, std::memory_order_relaxed);
    if (block) {
        gAllocBlocks.fetch_add(1, std::memory_order_relaxed);
        gAllocBytes.fetch_add(bytes, std::memory_order_relaxed);
    }
    if (tlsOperator == kNoOperator) {
        return;
    }
    TimedOperator &entry = gTimed[tlsOperator];
    entry.allocCalls.fetch_add(1, std::memory_order_relaxed);
    entry.allocNs.fetch_add(ns, std::memory_order_relaxed);
    if (block) {
        entry.allocBlocks.fetch_add(1, std::memory_order_relaxed);
        entry.allocBytes.fetch_add(bytes, std::memory_order_relaxed);
    }
    gAllocCallsInside.fetch_add(1, std::memory_order_relaxed);
    gAllocNsInside.fetch_add(ns, std::memory_order_relaxed);
    if (block) {
        gAllocBlocksInside.fetch_add(1, std::memory_order_relaxed);
        gAllocBytesInside.fetch_add(bytes, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------------------
// The timing thunks
// ---------------------------------------------------------------------------

operator_t profiledOperator(std::string_view uri, operator_t op) {
    if (op == nullptr || !detail::enabledFlag.load(std::memory_order_relaxed) || isThunk(op)) {
        return op;
    }
    auto it = gTimedIndex.find(std::string(uri));
    if (it != gTimedIndex.end()) {
        return gThunks[it->second];
    }
    if (gTimedCount >= kMaxTimedOperators) {
        gUntimedCalls.fetch_add(1, std::memory_order_relaxed);
        return op;
    }
    const size_t index             = gTimedCount++;
    gTimed[index].uri              = std::string(uri);
    gTimed[index].op               = op;
    gTimedIndex.emplace(gTimed[index].uri, index);
    return gThunks[index];
}

bool isThunk(operator_t op) noexcept {
    for (operator_t thunk : gThunks) {
        if (thunk == op) {
            return true;
        }
    }
    return false;
}

operator_t unwrapThunk(operator_t op) noexcept {
    for (size_t i = 0; i < gThunks.size(); ++i) {
        if (gThunks[i] == op) {
            return gTimed[i].op;
        }
    }
    return op;
}

std::string_view thunkUri(operator_t op) noexcept {
    for (size_t i = 0; i < gThunks.size(); ++i) {
        if (gThunks[i] == op) {
            return gTimed[i].uri;
        }
    }
    return {};
}

size_t untimedOperatorCalls() noexcept { return gUntimedCalls.load(std::memory_order_relaxed); }

// ---------------------------------------------------------------------------
// The reports
// ---------------------------------------------------------------------------

namespace {

/// The place in the source of the loop written at `origin`, or the origin number when the pass
/// did not note one (a loop the compiler synthesized).
std::string positionOf(uint64_t origin) {
    std::lock_guard lock(gMutex);
    if (auto it = gLoopPositions.find(origin); it != gLoopPositions.end()) {
        return it->second;
    }
    return origin == 0 ? std::string("(a synthesized loop)") : std::format("origin {}", origin);
}

std::string sourcePosition(const std::string &graph, uint64_t origin) {
    const std::string position = positionOf(origin);
    if (graph.empty()) {
        return position;
    }
    return std::format("{} ({})", position, graph);
}

std::string deviceText() {
    std::lock_guard lock(gMutex);
    return gDevice;
}

std::string backendText() {
    std::lock_guard lock(gMutex);
    return gBackend.empty() ? std::string("(not reported)") : gBackend;
}

std::vector<HostLoopHint> hints() {
    std::lock_guard lock(gMutex);
    return gHints;
}

/// The device an operator ran on: an operator is CPU code even when it drives the device (a
/// `tiles:` operator uploads, launches and waits), so the column names the path the call
/// belongs to.
std::string deviceOf(const std::string &uri) {
    const std::string device = deviceText();
    if (device != "cpu" && uri.starts_with("tiles:")) {
        return device.starts_with("cuda") ? std::string("cuda") : device;
    }
    return "cpu";
}

/// The fusion groups that ran, as a report reads them: the attribute that names each, the
/// operators of its members in execution order (the tail last), and its executions. The rows are
/// what the report prints; the recording itself is in `gFusionGroups`.
struct FusionRow {
    std::string key;
    std::string name;
    std::vector<std::string> members;
    uint64_t runs = 0;
    uint64_t ns = 0;
    uint64_t firstNs = 0;
};

std::vector<FusionRow> fusionGroupRows() {
    std::vector<FusionRow> rows;
    std::lock_guard lock(gFusionMutex);
    const size_t count = gFusionCount.load(std::memory_order_acquire);
    rows.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const FusionGroupStat &entry = gFusionGroups[i];
        const uint64_t runs          = entry.runs.load(std::memory_order_relaxed);
        if (runs == 0) {
            continue;
        }
        rows.push_back(FusionRow{
            entry.key,
            entry.name,
            entry.members,
            runs,
            entry.ns.load(std::memory_order_relaxed),
            entry.firstNs.load(std::memory_order_relaxed)});
    }
    std::sort(rows.begin(), rows.end(), [](const FusionRow &a, const FusionRow &b) {
        if (a.ns != b.ns) {
            return a.ns > b.ns;
        }
        if (a.runs != b.runs) {
            return a.runs > b.runs;
        }
        return a.name < b.name;
    });
    return rows;
}

/// The members' operators as one line: the first `limit` of them.
std::string joinedMembers(const std::vector<std::string> &members, size_t limit = 8) {
    std::string out;
    for (size_t i = 0; i < members.size(); ++i) {
        if (i == limit) {
            out += std::format(", ... {} more", members.size() - limit);
            break;
        }
        out += (i == 0 ? "" : ", ") + members[i];
    }
    return out;
}

} // namespace

void reportText(std::ostream &os, const ReportOptions &options) {
    measureOverhead();
    const uint64_t runNs      = nowNs();
    const uint64_t operators  = gOperators.load(std::memory_order_relaxed);
    const uint64_t operatorNs = gOperatorNs.load(std::memory_order_relaxed);
    const uint64_t kernels    = gKernels.load(std::memory_order_relaxed);
    const uint64_t kernelNs   = gKernelNs.load(std::memory_order_relaxed);
    const uint64_t toDevice   = gToDeviceBytes.load(std::memory_order_relaxed);
    const uint64_t toHost     = gToHostBytes.load(std::memory_order_relaxed);
    const uint64_t transfers  = gTransfers.load(std::memory_order_relaxed);
    const uint64_t syncs      = gSyncs.load(std::memory_order_relaxed);
    const uint64_t deviceCompiles = gDeviceCompiles.load(std::memory_order_relaxed);
    const uint64_t deviceCompileNs = gDeviceCompileNs.load(std::memory_order_relaxed);
    const uint64_t untimed        = gUntimedCalls.load(std::memory_order_relaxed);
    const uint64_t allocCalls     = gAllocCalls.load(std::memory_order_relaxed);
    const uint64_t allocBlocks    = gAllocBlocks.load(std::memory_order_relaxed);
    const uint64_t allocBytes     = gAllocBytes.load(std::memory_order_relaxed);
    const uint64_t allocNs        = gAllocNs.load(std::memory_order_relaxed);
    const uint64_t allocInside    = gAllocCallsInside.load(std::memory_order_relaxed);
    const uint64_t allocBlocksIn  = gAllocBlocksInside.load(std::memory_order_relaxed);
    const uint64_t allocBytesIn   = gAllocBytesInside.load(std::memory_order_relaxed);
    const uint64_t allocNsIn      = gAllocNsInside.load(std::memory_order_relaxed);
    const uint64_t fusionRuns     = gFusionRuns.load(std::memory_order_relaxed);
    const uint64_t fusionOps      = gFusionOperators.load(std::memory_order_relaxed);
    const uint64_t fusionNs       = gFusionNs.load(std::memory_order_relaxed);
    const std::vector<FusionRow> groupRows = fusionGroupRows();

    os << "PROFILE camel\n";
    os << std::format("  backend              {}\n", backendText());
    os << std::format("  device               {}\n", deviceText());
    os << std::format("  run                  {}\n", formatDuration(runNs));
    os << std::format(
        "  operator calls       {} (time inside them: {}, {} of the run)\n",
        operators,
        formatDuration(operatorNs),
        percentOf(operatorNs, runNs));
    if (fusionRuns != 0) {
        // A group's kernel is not an operator call: its time is not in the line above, and its
        // members have no operator row. The row of each group follows the operator table.
        os << std::format(
            "  fusion groups        {} in {} ({} operator executions), {} ({} of the run)\n",
            fusionRuns == 1 ? "1 run" : std::format("{} runs", fusionRuns),
            groupRows.size() == 1 ? "1 group" : std::format("{} groups", groupRows.size()),
            fusionOps,
            formatDuration(fusionNs),
            percentOf(fusionNs, runNs));
    }
    os << std::format(
        "  measurement overhead {:.0f} ns per operator call or allocation (one timestamp pair)\n",
        gOverheadNs);
    if (untimed != 0) {
        os << std::format(
            "  untimed calls        {} (more than {} distinct operators ran)\n",
            untimed,
            kMaxTimedOperators);
    }
    os << std::format(
        "  allocations          {} calls ({} block payloads), {} requested, {} inside the "
        "allocators\n",
        allocCalls,
        allocBlocks,
        formatBytes(allocBytes),
        formatDuration(allocNs));
    os << std::format(
        "                       inside operator calls: {} calls ({} block payloads, {}), {}; the "
        "rows below\n",
        allocInside,
        allocBlocksIn,
        formatBytes(allocBytesIn),
        formatDuration(allocNsIn));
    os << std::format(
        "  device kernels       {}{}\n",
        kernels,
        kernelNs == 0 ? std::string() : std::format(" ({})", formatDuration(kernelNs)));
    os << std::format(
        "  transfers            {} in all: host->device {}, device->host {}\n",
        transfers,
        formatBytes(toDevice),
        formatBytes(toHost));
    os << std::format("  synchronisations     {}\n", syncs);
    // In-place updates (docs/design/arrays-memory/README.cn.md 4.3.1): the marks of the plans
    // built during the run, then the answers of the update operators that asked the array itself.
    // A run that planned none and asked none prints nothing here, so a report of a program that
    // has no update stays what it was.
    const uint64_t plannedReuse = gUpdatePlannedReuse.load(std::memory_order_relaxed);
    const uint64_t plannedCount = gUpdatePlannedByCount.load(std::memory_order_relaxed);
    const uint64_t plannedCopy  = gUpdatePlannedCopy.load(std::memory_order_relaxed);
    const uint64_t countInPlace = gArrayCountInPlace.load(std::memory_order_relaxed);
    const uint64_t countCopied  = gArrayCountCopied.load(std::memory_order_relaxed);
    if (plannedReuse + plannedCount + plannedCopy + countInPlace + countCopied != 0) {
        os << std::format(
            "  in-place updates     the plan reuses the input's storage at {} update nodes, asks "
            "the array's count at {}, copies at {}\n",
            plannedReuse,
            plannedCount,
            plannedCopy);
        os << std::format(
            "                       the array's own count answered the update operators: allowed "
            "the write {} times, refused it {} times\n",
            countInPlace,
            countCopied);
    }
    if (deviceCompiles != 0) {
        os << std::format(
            "  device compiles      {} ({})\n", deviceCompiles, formatDuration(deviceCompileNs));
    }

    if (const LoopStat *step = stepLoop(); step != nullptr) {
        const double perRoundOps =
            static_cast<double>(step->opsSum) / static_cast<double>(step->samples);
        os << std::format(
            "\nsteps - the loop at {}: {} rounds ({} measured)\n",
            sourcePosition("", step->origin),
            step->rounds,
            step->samples);
        os << std::format(
            "  operators per step   {:.0f} (min {}, max {})\n",
            perRoundOps,
            step->opsMin == UINT64_MAX ? 0 : step->opsMin,
            step->opsMax);
        if (step->fusionRunsSum != 0) {
            // A step's operator executions are the calls of the operator rows plus what its
            // groups stood for; the first line counts only the calls.
            const double fusionPerRound = step->samples == 0
                                              ? 0.0
                                              : static_cast<double>(step->fusionOpsSum) / step->samples;
            os << std::format(
                "  operator executions per step {:.0f} ({:.0f} calls, {:.0f} in fusion groups, "
                "{:.2f} group runs)\n",
                perRoundOps + fusionPerRound,
                perRoundOps,
                fusionPerRound,
                step->samples == 0
                    ? 0.0
                    : static_cast<double>(step->fusionRunsSum) / static_cast<double>(step->samples));
        }
        os << std::format(
            "  kernels per step     {:.2f} (max {})\n",
            step->samples == 0 ? 0.0
                               : static_cast<double>(step->kernelsSum) / step->samples,
            step->kernelsMax);
        os << std::format(
            "  transfers per step   {:.2f} ({} host->device, {} device->host, max {} bytes)\n",
            step->samples == 0
                ? 0.0
                : static_cast<double>(step->toDeviceSum + step->toHostSum) / step->samples,
            step->toDeviceSum,
            step->toHostSum,
            std::max(step->toDeviceMax, step->toHostMax));
        os << std::format(
            "  syncs per step       {:.2f} (max {})\n",
            step->samples == 0 ? 0.0 : static_cast<double>(step->syncsSum) / step->samples,
            step->syncsMax);
        os << std::format(
            "  allocations per step {:.1f} calls ({:.1f} block payloads, {} payload, {} in the "
            "allocators)\n",
            step->samples == 0 ? 0.0
                               : static_cast<double>(step->allocCallsSum) / step->samples,
            step->samples == 0 ? 0.0
                               : static_cast<double>(step->allocBlocksSum) / step->samples,
            step->samples == 0 ? std::string("0 B") : formatBytes(step->allocBytesSum / step->samples),
            step->samples == 0 ? std::string("0 ns")
                               : formatDuration(step->allocNsSum / step->samples));
        if (step->samples != 0) {
            os << std::format(
                "  wall per step        median {}, mean {}, min {}, max {}\n",
                formatDuration(step->medianWallNs()),
                formatDuration(step->wallSumNs / step->samples),
                formatDuration(step->wallMinNs),
                formatDuration(step->wallMaxNs));
        }
        if (gLoops.size() > 1) {
            os << "  other loops that ran\n";
            for (const LoopStat &loop : gLoops) {
                if (&loop == step) {
                    continue;
                }
                os << std::format(
                    "    {} - {} rounds, {:.0f} operators per round{}\n",
                    sourcePosition("", loop.origin),
                    loop.rounds,
                    loop.samples == 0
                        ? 0.0
                        : static_cast<double>(loop.opsSum) / static_cast<double>(loop.samples),
                    loop.samples == 0 ? " (one round: no step numbers)" : "");
            }
        }
    } else {
        os << "\nsteps - the program ran no loop: the whole run is one step\n";
    }

    const std::vector<Row> rows = operatorRows();
    os << "\noperators (device, calls, total, first call, mean after the first, share of operator "
          "time,\n"
          "           allocation: time (share of the row), allocator calls, block payloads, "
          "payload bytes)\n";
    const size_t shown = options.top == 0 ? rows.size() : std::min(rows.size(), options.top);
    for (size_t i = 0; i < shown; ++i) {
        const Row &row         = rows[i];
        const uint64_t repeats = row.calls > 1 ? row.calls - 1 : 0;
        const uint64_t rest    = row.ns > row.firstNs ? row.ns - row.firstNs : 0;
        const std::string allocTime =
            std::format("{} ({})", formatDuration(row.allocNs), percentOf(row.allocNs, row.ns));
        os << std::format(
            "  {:<4} {:<28} {:>10} {:>12} {:>12} {:>12} {:>7} {:>18} {:>10} {:>10} {:>12}\n",
            row.device,
            row.uri,
            row.calls,
            formatDuration(row.ns),
            formatDuration(row.firstNs),
            repeats == 0 ? std::string("-") : formatDuration(rest / repeats),
            percentOf(row.ns, operatorNs),
            allocTime,
            row.allocCalls,
            row.allocBlocks,
            formatBytes(row.allocBytes));
    }
    if (shown < rows.size()) {
        os << std::format("  ... {} more operator(s)\n", rows.size() - shown);
    }

    if (!groupRows.empty()) {
        // A group is not an operator: this table says what its kernel cost and what it stood for,
        // where the operator table cannot (profile.h). Its members' rows would claim calls that
        // never happened, so there are none.
        os << "\nfusion groups (one pass each; its members' operators did not run as calls)\n";
        for (const FusionRow &group : groupRows) {
            const uint64_t executions = group.runs * group.members.size();
            os << std::format(
                "  {}=\"{}\"  {} member{} ({})\n",
                group.key,
                group.name,
                group.members.size(),
                group.members.size() == 1 ? "" : "s",
                joinedMembers(group.members));
            os << std::format(
                "    {} ({} operator executions), {} ({} of the run, first {})\n",
                group.runs == 1 ? "1 run" : std::format("{} runs", group.runs),
                executions,
                formatDuration(group.ns),
                percentOf(group.ns, runNs),
                formatDuration(group.firstNs));
        }
        if (const uint64_t overflow = gFusionOverflowGroups.load(std::memory_order_relaxed);
            overflow != 0) {
            // The table has a bound like the operator table's: say what it could not list rather
            // than let the rows look complete.
            os << std::format(
                "  ... {} group run(s) of groups beyond the first {}\n", overflow, kMaxFusionGroups);
        }
    }

    if (options.hints) {
        const std::vector<HostLoopHint> loopHints = hints();
        if (!loopHints.empty()) {
            os << "\nhints - loops that run on the host element by element\n";
            for (const HostLoopHint &hint : loopHints) {
                const LoopStat *stat = nullptr;
                if (auto it = gLoopIndex.find(hint.origin); it != gLoopIndex.end()) {
                    stat = &gLoops[it->second];
                }
                os << std::format(
                    "  {} - {}\n",
                    sourcePosition(hint.graph, hint.origin),
                    hint.tensor ? "element-wise loop over a tensor" : "element-wise loop");
                os << std::format("    operators        {}\n", hint.operators);
                os << std::format(
                    "    rounds           {}\n",
                    stat != nullptr ? std::to_string(stat->rounds) : std::string("none (the loop did not run)"));
                os << std::format("    why on the host  {}\n", hint.reason);
            }
        }
    }

    // The tees of the run, one line each, in the order the run first heard of them (profile.h). A
    // program that writes no `tee` records none and prints nothing here, so its report is what it
    // was.
    {
        std::vector<std::pair<uint64_t, StreamTee>> tees;
        {
            std::lock_guard lock(gStreamTeeMutex);
            tees.reserve(gStreamTees.size());
            for (const auto &[number, tee] : gStreamTees) {
                tees.emplace_back(number, tee);
            }
        }
        if (!tees.empty()) {
            os << "\nstreams - one line per tee (element counts; bytes where one element's size is "
                  "known)\n";
            for (const auto &[number, tee] : tees) {
                const std::string size =
                    tee.elemBytes == 0
                        ? std::string()
                        : std::format(
                              ",\"elem_bytes\":{},\"bytes\":{}", tee.elemBytes, tee.elemBytes * tee.peak);
                os << std::format(
                          "STREAM_TEE {{\"tee\":{},\"elem\":\"{}\",\"elements\":{},\"peak\":{}",
                          number,
                          tee.elem,
                          tee.elements,
                          tee.peak)
                   << size
                   << std::format(",\"hint\":{},\"cap\":{}}}\n", tee.hint, tee.cap);
            }
        }
    }

    // The compilation phases: the recorder keeps one entry per phase, self time computed here
    // (a phase's wall time minus that of the phases nested in it).
    if (camel::utils::phase::enabled()) {
        const std::vector<camel::utils::phase::PhaseRecord> phases = camel::utils::phase::snapshot();
        if (!phases.empty()) {
            os << "\ncompilation - self time by phase\n";
            std::vector<uint64_t> self(phases.size(), 0);
            for (size_t i = 0; i < phases.size(); ++i) {
                uint64_t children = 0;
                for (size_t j = i + 1; j < phases.size() && phases[j].depth > phases[i].depth; ++j) {
                    if (phases[j].depth == phases[i].depth + 1) {
                        children += phases[j].wallNs;
                    }
                }
                self[i] = phases[i].wallNs > children ? phases[i].wallNs - children : 0;
            }
            for (size_t i = 0; i < phases.size(); ++i) {
                const auto &phase = phases[i];
                std::string name  = phase.name;
                if (!phase.detail.empty()) {
                    name += " " + phase.detail;
                }
                os << std::format("  {:<36} {:>12}\n", name, formatDuration(self[i]));
            }
        }
    }
    os << std::flush;
    // The JSON form, when the pass was given a file to write it to. Written before the recording
    // stops, so it reads the same numbers the text above did.
    if (const std::string path = jsonPath(); !path.empty()) {
        std::ofstream out(camel::utils::pathFromUtf8(path), std::ios::binary);
        if (out) {
            out << reportJson(options) << "\n";
        } else {
            std::cerr << "camel: cannot write the profile report to " << path << std::endl;
        }
    }
}

std::string reportJson(const ReportOptions &options) {
    measureOverhead();
    std::string json = "{\"schema\":1";
    json += std::format(",\"backend\":\"{}\"", backendText());
    json += std::format(",\"device\":\"{}\"", deviceText());
    json += std::format(",\"run_ns\":{}", nowNs());
    json += std::format(
        ",\"operators\":{{\"calls\":{},\"ns\":{},\"overhead_ns\":{:.0f},\"untimed\":{}}}",
        gOperators.load(),
        gOperatorNs.load(),
        gOverheadNs,
        gUntimedCalls.load());
    if (const uint64_t fusionRuns = gFusionRuns.load(std::memory_order_relaxed);
        fusionRuns != 0) {
        // A group run is an operator execution, not an operator call: `calls` keeps counting the
        // calls of the operator rows, and `executions` adds the work the groups stood for. The
        // group's own time is `fusion_ns`; it is not inside `ns`.
        json += std::format(
            ",\"operator_executions\":{{\"calls\":{},\"fusion_runs\":{},\"fusion_operators\":{},"
            "\"executions\":{}}}",
            gOperators.load(),
            fusionRuns,
            gFusionOperators.load(),
            gOperators.load() + gFusionOperators.load());
    }
    json += std::format(
        ",\"allocations\":{{\"calls\":{},\"blocks\":{},\"bytes\":{},\"ns\":{},"
        "\"inside_calls\":{},\"inside_blocks\":{},\"inside_bytes\":{},\"inside_ns\":{}}}",
        gAllocCalls.load(),
        gAllocBlocks.load(),
        gAllocBytes.load(),
        gAllocNs.load(),
        gAllocCallsInside.load(),
        gAllocBlocksInside.load(),
        gAllocBytesInside.load(),
        gAllocNsInside.load());
    json += std::format(
        ",\"device_work\":{{\"kernels\":{},\"kernel_ns\":{},\"to_device_bytes\":{},"
        "\"to_host_bytes\":{},\"transfers\":{},\"syncs\":{},\"compiles\":{},\"compile_ns\":{}}}",
        gKernels.load(),
        gKernelNs.load(),
        gToDeviceBytes.load(),
        gToHostBytes.load(),
        gTransfers.load(),
        gSyncs.load(),
        gDeviceCompiles.load(),
        gDeviceCompileNs.load());
    // The same condition as the text report's in-place line: a run with no in-place update has
    // nothing to say here.
    if (gUpdatePlannedReuse.load(std::memory_order_relaxed) +
            gUpdatePlannedByCount.load(std::memory_order_relaxed) +
            gUpdatePlannedCopy.load(std::memory_order_relaxed) +
            gArrayCountInPlace.load(std::memory_order_relaxed) +
            gArrayCountCopied.load(std::memory_order_relaxed) !=
        0) {
        json += std::format(
            ",\"inplace_updates\":{{\"plan_reuse_input\":{},\"plan_by_array_count\":{},"
            "\"plan_copy\":{},\"array_count_one\":{},\"array_count_not_one\":{}}}",
            gUpdatePlannedReuse.load(std::memory_order_relaxed),
            gUpdatePlannedByCount.load(std::memory_order_relaxed),
            gUpdatePlannedCopy.load(std::memory_order_relaxed),
            gArrayCountInPlace.load(std::memory_order_relaxed),
            gArrayCountCopied.load(std::memory_order_relaxed));
    }
    if (const LoopStat *step = stepLoop(); step != nullptr) {
        json += std::format(
            ",\"step\":{{\"origin\":{},\"rounds\":{},\"samples\":{},\"operators_per_round\":{:.3f},"
            "\"kernels_per_round\":{:.3f},\"transfers_per_round\":{:.3f},\"syncs_per_round\":{:.3f},"
            "\"wall_per_round_ns\":{},\"allocations_per_round\":{{\"calls\":{:.3f},"
            "\"blocks\":{:.3f},\"bytes\":{:.3f},\"ns\":{:.3f}}}}}",
            step->origin,
            step->rounds,
            step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->opsSum) / step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->kernelsSum) / step->samples,
            step->samples == 0
                ? 0.0
                : static_cast<double>(step->toDeviceSum + step->toHostSum) / step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->syncsSum) / step->samples,
            step->samples == 0 ? 0 : step->wallSumNs / step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->allocCallsSum) / step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->allocBlocksSum) / step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->allocBytesSum) / step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->allocNsSum) / step->samples);
        if (step->fusionRunsSum != 0) {
            // The step's operator executions: the operator calls' part (`operators_per_round`,
            // printed above) and the part the groups stood for. A step with no group is exactly
            // the object it was before.
            json += std::format(
                ",\"step_fusion\":{{\"runs_per_round\":{:.3f},\"operators_per_round\":{:.3f},"
                "\"executions_per_round\":{:.3f}}}",
                static_cast<double>(step->fusionRunsSum) / step->samples,
                static_cast<double>(step->fusionOpsSum) / step->samples,
                static_cast<double>(step->opsSum + step->fusionOpsSum) / step->samples);
        }
    }
    json += ",\"loops\":[";
    for (size_t i = 0; i < gLoops.size(); ++i) {
        const LoopStat &loop = gLoops[i];
        json += std::format(
            "{}{{\"origin\":{},\"rounds\":{},\"samples\":{},\"operators_per_round\":{:.3f}}}",
            i == 0 ? "" : ",",
            loop.origin,
            loop.rounds,
            loop.samples,
            loop.samples == 0 ? 0.0 : static_cast<double>(loop.opsSum) / loop.samples);
    }
    json += "],\"operator_rows\":[";
    const std::vector<Row> rows = operatorRows();
    const size_t shown = options.top == 0 ? rows.size() : std::min(rows.size(), options.top);
    for (size_t i = 0; i < shown; ++i) {
        json += std::format(
            "{}{{\"uri\":\"{}\",\"device\":\"{}\",\"calls\":{},\"ns\":{},"
            "\"alloc_ns\":{},\"alloc_calls\":{},\"alloc_blocks\":{},\"alloc_bytes\":{}}}",
            i == 0 ? "" : ",",
            rows[i].uri,
            rows[i].device,
            rows[i].calls,
            rows[i].ns,
            rows[i].allocNs,
            rows[i].allocCalls,
            rows[i].allocBlocks,
            rows[i].allocBytes);
    }
    const std::vector<FusionRow> groups = fusionGroupRows();
    if (!groups.empty()) {
        // Only when a group ran: a program without groups has exactly the JSON it had before.
        json += "],\"fusion_groups\":[";
        for (size_t i = 0; i < groups.size(); ++i) {
            json += std::format(
                "{}{{\"key\":\"{}\",\"value\":\"{}\",\"members\":[",
                i == 0 ? "" : ",",
                groups[i].key,
                groups[i].name);
            for (size_t j = 0; j < groups[i].members.size(); ++j) {
                json += std::format(
                    "{}\"{}\"", j == 0 ? "" : ",", groups[i].members[j]);
            }
            json += std::format(
                "],\"runs\":{},\"ns\":{},\"first_ns\":{},\"operator_executions\":{}}}",
                groups[i].runs,
                groups[i].ns,
                groups[i].firstNs,
                groups[i].runs * groups[i].members.size());
        }
    }
    json += "],\"hints\":[";
    const std::vector<HostLoopHint> loopHints = hints();
    for (size_t i = 0; i < loopHints.size(); ++i) {
        json += std::format(
            "{}{{\"origin\":{},\"graph\":\"{}\",\"operators\":\"{}\",\"elementwise\":{},"
            "\"tensor\":{},\"reason\":\"{}\"}}",
            i == 0 ? "" : ",",
            loopHints[i].origin,
            loopHints[i].graph,
            loopHints[i].operators,
            loopHints[i].elementwise,
            loopHints[i].tensor,
            loopHints[i].reason);
    }
    json += "]}";
    return json;
}

} // namespace camel::profile