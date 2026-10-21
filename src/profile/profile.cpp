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
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
#include <mutex>
#include <ostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::profile {

namespace {

using Clock = std::chrono::steady_clock;

Clock::time_point gOrigin = Clock::now();

uint64_t nowNs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - gOrigin).count());
}

bool gEnabled = false;
std::thread::id gThread;

/// The counters a step boundary reads: what the run has done so far.
struct Totals {
    uint64_t operators = 0;
    uint64_t kernels   = 0;
    uint64_t toDevice  = 0; // bytes
    uint64_t toHost    = 0; // bytes
    uint64_t syncs     = 0;
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

std::mutex gMutex;
std::string gBackend;
std::string gDevice = "cpu";
std::vector<HostLoopHint> gHints;
std::unordered_map<uint64_t, std::string> gLoopPositions;
ReportOptions gOptions;
std::string gJsonPath;

Totals totalsNow() {
    Totals totals;
    totals.operators = gOperators.load(std::memory_order_relaxed);
    totals.kernels   = gKernels.load(std::memory_order_relaxed);
    totals.toDevice  = gToDeviceBytes.load(std::memory_order_relaxed);
    totals.toHost    = gToHostBytes.load(std::memory_order_relaxed);
    totals.syncs     = gSyncs.load(std::memory_order_relaxed);
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
            entry.firstNs.load(std::memory_order_relaxed)});
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

bool enabled() noexcept { return gEnabled; }

void enable() {
    if (gEnabled) {
        return;
    }
    gOrigin  = Clock::now();
    gThread  = std::this_thread::get_id();
    gEnabled = true;
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
    for (size_t i = 0; i < gTimedCount; ++i) {
        gTimed[i].calls.store(0);
        gTimed[i].ns.store(0);
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
    gOrigin = Clock::now();
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

double measurementOverheadNs() {
    measureOverhead();
    return gOverheadNs;
}

// ---------------------------------------------------------------------------
// The timing thunks
// ---------------------------------------------------------------------------

operator_t profiledOperator(std::string_view uri, operator_t op) {
    if (op == nullptr || !gEnabled || isThunk(op)) {
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

    os << "PROFILE camel\n";
    os << std::format("  backend              {}\n", backendText());
    os << std::format("  device               {}\n", deviceText());
    os << std::format("  run                  {}\n", formatDuration(runNs));
    os << std::format(
        "  operator calls       {} (time inside them: {}, {} of the run)\n",
        operators,
        formatDuration(operatorNs),
        percentOf(operatorNs, runNs));
    os << std::format(
        "  measurement overhead {:.0f} ns per operator call (one timestamp pair)\n", gOverheadNs);
    if (untimed != 0) {
        os << std::format(
            "  untimed calls        {} (more than {} distinct operators ran)\n",
            untimed,
            kMaxTimedOperators);
    }
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
          "time)\n";
    const size_t shown = options.top == 0 ? rows.size() : std::min(rows.size(), options.top);
    for (size_t i = 0; i < shown; ++i) {
        const Row &row         = rows[i];
        const uint64_t repeats = row.calls > 1 ? row.calls - 1 : 0;
        const uint64_t rest    = row.ns > row.firstNs ? row.ns - row.firstNs : 0;
        os << std::format(
            "  {:<4} {:<28} {:>10} {:>12} {:>12} {:>12} {:>7}\n",
            row.device,
            row.uri,
            row.calls,
            formatDuration(row.ns),
            formatDuration(row.firstNs),
            repeats == 0 ? std::string("-") : formatDuration(rest / repeats),
            percentOf(row.ns, operatorNs));
    }
    if (shown < rows.size()) {
        os << std::format("  ... {} more operator(s)\n", rows.size() - shown);
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
    if (const LoopStat *step = stepLoop(); step != nullptr) {
        json += std::format(
            ",\"step\":{{\"origin\":{},\"rounds\":{},\"samples\":{},\"operators_per_round\":{:.3f},"
            "\"kernels_per_round\":{:.3f},\"transfers_per_round\":{:.3f},\"syncs_per_round\":{:.3f},"
            "\"wall_per_round_ns\":{}}}",
            step->origin,
            step->rounds,
            step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->opsSum) / step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->kernelsSum) / step->samples,
            step->samples == 0
                ? 0.0
                : static_cast<double>(step->toDeviceSum + step->toHostSum) / step->samples,
            step->samples == 0 ? 0.0 : static_cast<double>(step->syncsSum) / step->samples,
            step->samples == 0 ? 0 : step->wallSumNs / step->samples);
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
            "{}{{\"uri\":\"{}\",\"device\":\"{}\",\"calls\":{},\"ns\":{}}}",
            i == 0 ? "" : ",",
            rows[i].uri,
            rows[i].device,
            rows[i].calls,
            rows[i].ns);
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