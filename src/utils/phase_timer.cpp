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
 * Created: Oct. 13, 2026
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The phase recorder behind camel/utils/phase_timer.h: a list of closed phases in the order
 * they were opened, a stack of the open ones, and a few named counters.
 */

#include "camel/utils/phase_timer.h"

#include "camel/utils/env.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <dlfcn.h>
#endif

namespace camel::utils::phase {

namespace detail {
/// Whether phases are being recorded. Read on every Scope; written once, before the first.
bool gEnabled = false;
}

bool enabled() { return detail::gEnabled; }

namespace {

using Clock = std::chrono::steady_clock;

/// What the counting allocator shim reports (test/tools/allocshim/allocshim.c): totals since
/// the process started. The layout is the shim's `camel_allocshim_counters`.
struct AllocCounters {
    uint64_t allocations = 0; // calls that returned memory: malloc, calloc, realloc, ...
    uint64_t bytes       = 0; // bytes requested by those calls
    uint64_t frees       = 0; // calls of free with a non-null pointer
    uint64_t liveBytes   = 0; // usable bytes allocated and not yet freed
    uint64_t peakBytes   = 0; // the highest liveBytes seen
};
using AllocSnapshotFn = void (*)(AllocCounters *);

struct Phase {
    std::string name;
    std::string detail;
    uint32_t depth  = 0; // number of phases open when this one was opened
    int64_t startNs = 0;
    int64_t wallNs  = -1;    // -1 while open
    AllocCounters atStart{}; // totals when the phase was opened
    AllocCounters atEnd{};
};

struct Recorder {
    std::string path;
    Clock::time_point origin;
    std::thread::id thread;
    std::vector<Phase> phases;   // in opening order; a parent precedes its children
    std::vector<size_t> open;    // indices into phases, innermost last
    std::vector<size_t> pending; // phases opened by open(), closed by closeOpen()
    std::map<std::string, uint64_t> counters;
    AllocSnapshotFn allocSnapshot = nullptr;
};

Recorder &recorder() {
    static Recorder instance;
    return instance;
}

int64_t nowNs(const Recorder &r) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - r.origin).count();
}

/// The shim exports its counters under this name when it is preloaded. It is looked up among
/// the symbols of the process, so that neither libcamel nor the tools link against it.
AllocSnapshotFn findAllocShim() {
#ifndef _WIN32
    return reinterpret_cast<AllocSnapshotFn>(dlsym(RTLD_DEFAULT, "camel_allocshim_counters"));
#else
    return nullptr;
#endif
}

/// Peak resident set size of the process in kilobytes; 0 where it cannot be read.
/// Read from /proc/self/status (VmHWM), which belongs to this program image: the maximum
/// getrusage reports also covers the process that started this one, before its exec.
uint64_t peakRssKb() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with("VmHWM:")) {
            return std::strtoull(line.c_str() + 6, nullptr, 10);
        }
    }
#endif
    return 0;
}

void appendJsonString(std::ostringstream &os, std::string_view text) {
    os << '"';
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            os << '\\' << c;
        } else if (static_cast<unsigned char>(c) < 0x20) {
            os << "\\u00" << "0123456789abcdef"[(c >> 4) & 0xF] << "0123456789abcdef"[c & 0xF];
        } else {
            os << c;
        }
    }
    os << '"';
}

void closeInnermost(Recorder &r) {
    Phase &phase = r.phases[r.open.back()];
    phase.wallNs = nowNs(r) - phase.startNs;
    if (r.allocSnapshot != nullptr) {
        r.allocSnapshot(&phase.atEnd);
    }
    r.open.pop_back();
}

} // namespace

void detail::begin(std::string_view name, std::string_view detail) {
    Recorder &r = recorder();
    if (std::this_thread::get_id() != r.thread) {
        return;
    }
    Phase phase;
    phase.name    = std::string(name);
    phase.detail  = std::string(detail);
    phase.depth   = static_cast<uint32_t>(r.open.size());
    phase.startNs = nowNs(r);
    if (r.allocSnapshot != nullptr) {
        r.allocSnapshot(&phase.atStart);
    }
    r.open.push_back(r.phases.size());
    r.phases.push_back(std::move(phase));
}

void detail::end() {
    Recorder &r = recorder();
    if (std::this_thread::get_id() != r.thread || r.open.empty()) {
        return;
    }
    closeInnermost(r);
}

void enable(const std::string &path) {
    Recorder &r = recorder();
    r.path      = path;
    if (detail::gEnabled) {
        return;
    }
    r.origin         = Clock::now();
    r.thread         = std::this_thread::get_id();
    r.allocSnapshot  = findAllocShim();
    detail::gEnabled = true;
}

void enableFromEnvironment() {
    if (const std::string path = getEnv("CAMEL_TIME_PHASES"); !path.empty()) {
        enable(path);
    }
}

void open(std::string_view name, std::string_view detail) {
    if (!enabled()) {
        return;
    }
    Recorder &r = recorder();
    if (std::this_thread::get_id() != r.thread) {
        return;
    }
    detail::begin(name, detail);
    r.pending.push_back(r.open.back());
}

void closeOpen() {
    if (!enabled()) {
        return;
    }
    Recorder &r = recorder();
    if (std::this_thread::get_id() != r.thread || r.pending.empty()) {
        return;
    }
    // Scopes nested in the pending phase have ended by now; anything still above it on the
    // stack was left open by an exception and ends with it.
    const size_t target = r.pending.back();
    r.pending.pop_back();
    while (!r.open.empty() && r.open.back() >= target) {
        closeInnermost(r);
    }
}

void count(std::string_view name, uint64_t value) {
    if (!enabled()) {
        return;
    }
    recorder().counters[std::string(name)] += value;
}

std::string toJson() {
    Recorder &r = recorder();
    std::ostringstream os;
    const int64_t now = nowNs(r);
    os << "{\"schema\":1,\"total_ns\":" << now << ",\"peak_rss_kb\":" << peakRssKb()
       << ",\"alloc_shim\":" << (r.allocSnapshot != nullptr ? "true" : "false");
    if (r.allocSnapshot != nullptr) {
        AllocCounters total;
        r.allocSnapshot(&total);
        os << ",\"alloc\":{\"allocations\":" << total.allocations << ",\"bytes\":" << total.bytes
           << ",\"frees\":" << total.frees << ",\"live_bytes\":" << total.liveBytes
           << ",\"peak_bytes\":" << total.peakBytes << "}";
    }
    os << ",\"phases\":[";
    for (size_t i = 0; i < r.phases.size(); ++i) {
        const Phase &phase = r.phases[i];
        os << (i == 0 ? "" : ",") << "\n{\"name\":";
        appendJsonString(os, phase.name);
        os << ",\"detail\":";
        appendJsonString(os, phase.detail);
        os << ",\"depth\":" << phase.depth << ",\"start_ns\":" << phase.startNs
           << ",\"wall_ns\":" << (phase.wallNs >= 0 ? phase.wallNs : now - phase.startNs);
        if (r.allocSnapshot != nullptr && phase.wallNs >= 0) {
            os << ",\"allocations\":" << phase.atEnd.allocations - phase.atStart.allocations
               << ",\"alloc_bytes\":" << phase.atEnd.bytes - phase.atStart.bytes
               << ",\"frees\":" << phase.atEnd.frees - phase.atStart.frees
               << ",\"live_bytes_end\":" << phase.atEnd.liveBytes
               << ",\"peak_bytes_end\":" << phase.atEnd.peakBytes;
        }
        os << "}";
    }
    os << "\n],\"counters\":{";
    bool first = true;
    for (const auto &[name, value] : r.counters) {
        os << (first ? "" : ",");
        appendJsonString(os, name);
        os << ":" << value;
        first = false;
    }
    os << "}}\n";
    return os.str();
}

void report() {
    if (!enabled()) {
        return;
    }
    Recorder &r = recorder();
    r.pending.clear();
    while (!r.open.empty()) {
        closeInnermost(r);
    }
    const std::string json = toJson();
    detail::gEnabled       = false;
    if (r.path == "-") {
        std::cerr << json << std::flush;
        return;
    }
    std::ofstream out(r.path, std::ios::binary);
    if (!out) {
        std::cerr << "camel: cannot write the phase report to " << r.path << std::endl;
        return;
    }
    out << json;
}

} // namespace camel::utils::phase
