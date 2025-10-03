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
 * Created: Sep. 27, 2025
 * Updated: Oct. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "trace.h"
#ifndef NDEBUG

#include <chrono>
#include <fstream>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>

namespace profiler {

static std::vector<TraceEvent> g_events;
static std::mutex g_mutex;

void trace_event_begin(const char *name, const char *category) {
    // std::cout << "[TRACE_DEBUG] trace_event_begin called with name: " << (name ? name :
    // "unnamed") << std::endl;

    std::lock_guard<std::mutex> lock(g_mutex);
    // std::cout << "[TRACE_DEBUG] Acquired mutex in trace_event_begin" << std::endl;

    TraceEvent e;
    e.name = name ? name : "unnamed";
    e.ph = "B";
    e.ts = std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::high_resolution_clock::now().time_since_epoch())
               .count();

    g_events.push_back(e);
    // std::cout << "[TRACE_DEBUG] Event added to g_events" << std::endl;
}

void trace_event_end() {
    // std::cout << "[TRACE_DEBUG] trace_event_end called" << std::endl;

    std::lock_guard<std::mutex> lock(g_mutex);
    // std::cout << "[TRACE_DEBUG] Acquired mutex in trace_event_end" << std::endl;

    TraceEvent e;
    e.ph = "E";
    e.ts = std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::high_resolution_clock::now().time_since_epoch())
               .count();

    g_events.push_back(e);
    // std::cout << "[TRACE_DEBUG] End event added to g_events" << std::endl;
}

void trace_event_instant(const char *name, const char *category) {
    std::lock_guard<std::mutex> lock(g_mutex);

    TraceEvent e;
    e.name = name ? name : "unnamed";
    e.ph = "i";
    e.ts = std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::high_resolution_clock::now().time_since_epoch())
               .count();

    g_events.push_back(e);
}

void trace_event_complete(
    const char *name, uint64_t timestamp, uint64_t duration, const char *category) {
    std::lock_guard<std::mutex> lock(g_mutex);

    TraceEvent e;
    e.name = name ? name : "unnamed";
    e.ph = "X";
    e.ts = timestamp;
    e.dur = duration;

    g_events.push_back(e);
}

void trace_event_counter(const char *name, int value, const char *category) {
    std::lock_guard<std::mutex> lock(g_mutex);

    TraceEvent e;
    e.name = name ? name : "unnamed";
    e.ph = "C";
    e.ts = std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::high_resolution_clock::now().time_since_epoch())
               .count();
    e.args = "{\"value\": " + std::to_string(value) + "}";

    g_events.push_back(e);
}
void trace_event_flush() { trace_event_flush(""); }
void trace_event_flush(const std::string &filename) {
    std::lock_guard<std::mutex> lock(g_mutex);

    nlohmann::json trace_data;
    trace_data["traceEvents"] = nlohmann::json::array();

    for (const auto &event : g_events) {
        nlohmann::json json_event;
        json_event["name"] = event.name;
        json_event["ph"] = event.ph;
        json_event["ts"] = event.ts;
        json_event["pid"] = event.pid;
        json_event["tid"] = event.tid;

        if (event.ph == "X" && event.dur > 0) {
            json_event["dur"] = event.dur;
        }

        if (!event.args.empty()) {
            try {
                json_event["args"] = nlohmann::json::parse(event.args);
            } catch (...) {
                json_event["args"] = event.args;
            }
        }

        trace_data["traceEvents"].push_back(json_event);
    }

    std::string output_filename = filename.empty() ? "profile_reports/trace_output.json" : filename;
    std::filesystem::path filepath(output_filename);
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path());
    }

    std::ofstream file(output_filename);
    if (file.is_open()) {
        file << std::setw(2) << trace_data << std::endl;
        file.close();
        // std::cout << "[PROFILER CORE] Trace data saved to " << output_filename << std::endl;
    }

    g_events.clear();
}

const std::vector<TraceEvent> &get_trace_events() { return g_events; }

} // namespace profiler

#endif // NDEBUG
