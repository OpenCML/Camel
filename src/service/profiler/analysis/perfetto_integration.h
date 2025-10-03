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

#ifndef NDEBUG

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace profiler {

struct PerfettoTraceEvent {
    std::string name;
    std::string category;
    char phase; // 'B'=Begin, 'E'=End, 'I'=Instant
    uint64_t timestamp;
    uint32_t thread_id;
    uint32_t process_id = 1;

    std::string args;
    uint64_t duration = 0;
};

class PerfettoIntegration {
  public:
    static PerfettoIntegration &getInstance();

    void
    startTracing(const std::string &output_file = "profile_reports/camel_trace.perfetto-trace");
    void stopAndOpenPerfetto();

    void recordEventBegin(const std::string &name, const std::string &category = "compiler");
    void recordEventEnd(const std::string &name, const std::string &category = "compiler");
    void recordEventInstant(const std::string &name, const std::string &category = "compiler");

    void generatePerfettoFile();

    void openPerfettoInBrowser(bool auto_open = true);

    PerfettoIntegration() = default;
    ~PerfettoIntegration() = default;

    friend class AdvancedTracer;

  private:
    bool tracing_enabled_ = false;
    std::string output_file_;
    std::vector<PerfettoTraceEvent> events_;

    uint64_t getCurrentTimestamp();
    uint32_t getCurrentThreadId();
};

#define PERFETTO_START(file) profiler::PerfettoIntegration::getInstance().startTracing(file)
#define PERFETTO_STOP_AND_OPEN() profiler::PerfettoIntegration::getInstance().stopAndOpenPerfetto()

#define PERFETTO_EVENT_BEGIN(name)                                                                 \
    profiler::PerfettoIntegration::getInstance().recordEventBegin(name)

#define PERFETTO_EVENT_END(name) profiler::PerfettoIntegration::getInstance().recordEventEnd(name)

#define PERFETTO_EVENT_INSTANT(name)                                                               \
    profiler::PerfettoIntegration::getInstance().recordEventInstant(name)

} // namespace profiler

#else

#define PERFETTO_START(file)
#define PERFETTO_STOP_AND_OPEN()
#define PERFETTO_EVENT_BEGIN(name)
#define PERFETTO_EVENT_END(name)
#define PERFETTO_EVENT_INSTANT(name)

#endif
