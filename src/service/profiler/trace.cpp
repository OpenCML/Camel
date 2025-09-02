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
 * Created: Aug. 20, 2025
 * Updated: Aug. 20, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "trace.h"
#include "event.h"

#include <chrono>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

using namespace std::chrono;

static std::vector<TraceEvent> g_events;
static std::mutex g_mutex;

static uint64_t now_us() {
    return duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
}

static int get_pid() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentProcessId());
#else
    return static_cast<int>(getpid());
#endif
}

static int get_tid() {
#ifdef _WIN32
    return static_cast<int>(GetCurrentThreadId());
#else
    return static_cast<int>(syscall(SYS_gettid));
#endif
}

void trace_event_begin(const std::string &name, const std::string &cat) {
    TraceEvent e;
    e.name = name;
    e.cat = cat;
    e.ph = "B";
    e.ts = now_us();
    e.pid = get_pid();
    e.tid = get_tid();

    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back(e);
}

void trace_event_end(const std::string &name) {
    TraceEvent e;
    e.name = name;
    e.cat = "default";
    e.ph = "E";
    e.ts = now_us();
    e.pid = get_pid();
    e.tid = get_tid();

    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back(e);
}

void trace_event_instant(
    const std::string &name, const std::string &cat, const std::string &scope) {
    TraceEvent e;
    e.name = name;
    e.cat = cat;
    e.ph = "i";
    e.ts = now_us();
    e.pid = get_pid();
    e.tid = get_tid();
    e.scope = scope;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back(e);
}

void trace_event_complete(
    const std::string &name, uint64_t start_ts, uint64_t dur, const std::string &cat) {
    TraceEvent e;
    e.name = name;
    e.cat = cat;
    e.ph = "X";
    e.ts = start_ts;
    e.dur = dur;
    e.pid = get_pid();
    e.tid = get_tid();

    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back(e);
}

void trace_event_counter(const std::string &name, int value, const std::string &cat) {
    TraceEvent e;
    e.name = name;
    e.cat = cat;
    e.ph = "C";
    e.ts = now_us();
    e.pid = get_pid();
    e.tid = get_tid();
    e.args["value"] = std::to_string(value);

    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back(e);
}

void trace_event_metadata_thread_name(const std::string &name) {
    TraceEvent e;
    e.name = "thread_name";
    e.cat = "";
    e.ph = "M";
    e.ts = 0;
    e.pid = get_pid();
    e.tid = get_tid();
    e.args["name"] = name;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back(e);
}

void trace_event_metadata_process_name(const std::string &name) {
    TraceEvent e;
    e.name = "process_name";
    e.cat = "";
    e.ph = "M";
    e.ts = 0;
    e.pid = get_pid();
    e.tid = 0;
    e.args["name"] = name;

    std::lock_guard<std::mutex> lock(g_mutex);
    g_events.push_back(e);
}

void trace_event_flush() {
    std::lock_guard<std::mutex> lock(g_mutex);
    std::ofstream out("trace_output.json");
    out << "{\n\"traceEvents\": [\n";
    for (size_t i = 0; i < g_events.size(); ++i) {
        out << g_events[i].to_json().dump();
        if (i + 1 < g_events.size())
            out << ",\n";
    }
    out << "\n]}\n";
    out.close();
    g_events.clear();
}
