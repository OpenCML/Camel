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

#pragma once

#include "event.h"
#include "nlohmann/json.hpp"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <string>
#include <vector>

#ifndef NDEBUG

namespace profiler {

struct TraceEvent {
    std::string name;
    std::string ph;   // Phase: B=begin, E=end, i=instant, X=complete, C=counter
    uint64_t ts;      // Timestamp in microseconds
    uint64_t dur = 0; // Duration in microseconds (for complete events)
    uint32_t pid = 1;
    uint32_t tid = 0;
    std::string args; // JSON string for arguments
};

void trace_event_begin(const char *name, const char *category = "compiler");
void trace_event_end();
void trace_event_instant(const char *name, const char *category = "compiler");
void trace_event_complete(
    const char *name, uint64_t timestamp, uint64_t duration, const char *category = "compiler");
void trace_event_counter(const char *name, int value, const char *category = "compiler");
void trace_event_flush();
void trace_event_flush(const std::string &filename);

const std::vector<TraceEvent> &get_trace_events();

#define TRACE_EVENT_BEGIN(name) trace_event_begin(name)
#define TRACE_EVENT_END() trace_event_end()
#define TRACE_EVENT_INSTANT(name) trace_event_instant(name)
#define TRACE_EVENT_COMPLETE(name, timestamp, duration)                                            \
    trace_event_complete(name, timestamp, duration)
#define TRACE_EVENT_COUNTER(name, value) trace_event_counter(name, value)
#define TRACE_FLUSH() trace_event_flush()

#define TRACE_FUNCTION()                                                                           \
    struct TraceFunctionHelper {                                                                   \
        TraceFunctionHelper(const char *name) : name_(name) { TRACE_EVENT_BEGIN(name_); }          \
        ~TraceFunctionHelper() { TRACE_EVENT_END(); }                                              \
        const char *name_;                                                                         \
    } trace_function_helper_(__FUNCTION__)

} // namespace profiler

#else

#define TRACE_EVENT_BEGIN(name)
#define TRACE_EVENT_END()
#define TRACE_EVENT_INSTANT(name)
#define TRACE_EVENT_COMPLETE(name, timestamp, duration)
#define TRACE_EVENT_COUNTER(name, value)
#define TRACE_FLUSH()
#define TRACE_FUNCTION()

#endif
