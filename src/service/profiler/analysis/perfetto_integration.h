// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>

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
    uint32_t threadId;
    uint32_t processId = 1;

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
    bool tracingEnabled_ = false;
    // --profile-mode full: open the UI when the trace is written, instead of only writing it.
    bool autoOpen_ = false;
    std::string outputFile_;
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
