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
 * Updated: Oct. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#ifndef NDEBUG

#include "../analysis/perfetto_integration.h"
#include "../analysis/statistics.h"
#include "../core/trace.h"
#include "../tracers/call_tracer.h"
#include <memory>
#include <string>
#include <unordered_set>

namespace profiler {

class AdvancedTracer {
  public:
    static AdvancedTracer &getInstance();

    // Configuration options
    struct Config {
        bool enable_function_tracing = true;
        bool enable_memory_tracing = true;
        bool enable_io_tracing = true;
        bool enable_exception_tracing = true;
        bool enable_statistics = true;
        bool enable_perfetto_integration = true;

        int max_call_depth = 100;
        uint64_t min_duration_us = 0;
        double sampling_rate = 1.0;
        size_t max_samples = 1000;

        std::string output_file = "profile_reports/camel_trace.json";
        std::string perfetto_output = "profile_reports/camel_trace.perfetto-trace";

        std::unordered_set<std::string> include_functions;
        std::unordered_set<std::string> exclude_functions;

        bool real_time_analysis = false;
        bool generate_call_graph = true;
        bool generate_summary_report = true;
    };

    // Configuration initialization
    void initialize(const Config &config);
    void initializeWithDefaults();

    // Tracing control
    void startTracing();
    void stopTracing();
    void pauseTracing();
    void resumeTracing();
    bool isTracing() const;

    // Advanced tracing functions
    void
    traceFunctionCall(const std::string &func_name, const std::string &file = "", int line = 0);
    void traceFunctionReturn(const std::string &func_name);

    void traceMemoryAllocation(void *ptr, size_t size, const std::string &type = "");
    void traceMemoryDeallocation(void *ptr);

    void traceFileOperation(
        const std::string &operation, const std::string &filename, size_t bytes = 0,
        uint64_t duration_us = 0);
    void traceNetworkOperation(
        const std::string &operation, const std::string &endpoint, size_t bytes = 0,
        uint64_t duration_us = 0);

    void traceException(const std::string &exception_type, const std::string &message);
    void traceExceptionHandled(const std::string &exception_type);

    // Performance metrics
    void recordMetric(const std::string &name, double value);
    void incrementCounter(const std::string &name, int64_t increment = 1);

    // Report generation
    void generateTraceFile();
    void generateStatisticsReport();
    void generateCallGraph();
    void openPerfettoUI();

    // Real-time analysis
    void enableRealTimeAnalysis(bool enable);
    void setSamplingRate(double rate);
    void setOutputFile(const std::string &filename);

    // Filters
    void addFunctionFilter(const std::string &func_name, bool include = true);
    void removeFunctionFilter(const std::string &func_name);
    void clearFunctionFilters();

    // Status query
    const Config &getConfig() const;
    uint64_t getTraceDuration() const;
    size_t getTraceEventCount() const;
    size_t getMemoryUsage() const;

  private:
    AdvancedTracer() = default;
    ~AdvancedTracer() = default;

    bool shouldTraceFunction(const std::string &func_name) const;
    void recordTraceEvent(
        const std::string &name, const std::string &category, const std::string &phase,
        uint64_t timestamp, uint64_t duration = 0);

    Config config_;
    bool tracing_enabled_ = false;
    bool tracing_paused_ = false;

    uint64_t trace_start_time_ = 0;
    uint64_t trace_end_time_ = 0;
    size_t trace_event_count_ = 0;

    std::unique_ptr<PerfettoIntegration> perfetto_integration_;
};

void start_advanced_tracing(const AdvancedTracer::Config &config = AdvancedTracer::Config());
void stop_advanced_tracing();
void generate_advanced_report(const std::string &output_file = "");

// Advanced tracing macros
#define ADVANCED_TRACE_START(config) profiler::start_advanced_tracing(config)
#define ADVANCED_TRACE_STOP() profiler::stop_advanced_tracing()
#define ADVANCED_TRACE_REPORT(output_file) profiler::generate_advanced_report(output_file)

#define ADVANCED_TRACE_FUNCTION()                                                                  \
    if (profiler::AdvancedTracer::getInstance().isTracing()) {                                     \
        profiler::AdvancedTracer::getInstance().traceFunctionCall(                                 \
            __FUNCTION__,                                                                          \
            __FILE__,                                                                              \
            __LINE__);                                                                             \
    }                                                                                              \
    auto __profiler_advanced_trace_guard = profiler::ScopeGuard([&]() {                            \
        if (profiler::AdvancedTracer::getInstance().isTracing()) {                                 \
            profiler::AdvancedTracer::getInstance().traceFunctionReturn(__FUNCTION__);             \
        }                                                                                          \
    })

#define ADVANCED_TRACE_MEMORY_ALLOC(ptr, size, type)                                               \
    if (profiler::AdvancedTracer::getInstance().isTracing()) {                                     \
        profiler::AdvancedTracer::getInstance().traceMemoryAllocation(ptr, size, type);            \
    }

#define ADVANCED_TRACE_MEMORY_FREE(ptr)                                                            \
    if (profiler::AdvancedTracer::getInstance().isTracing()) {                                     \
        profiler::AdvancedTracer::getInstance().traceMemoryDeallocation(ptr);                      \
    }

#define ADVANCED_TRACE_FILE_OP(operation, filename, bytes, duration_us)                            \
    if (profiler::AdvancedTracer::getInstance().isTracing()) {                                     \
        profiler::AdvancedTracer::getInstance()                                                    \
            .traceFileOperation(operation, filename, bytes, duration_us);                          \
    }

#define ADVANCED_TRACE_EXCEPTION(type, message)                                                    \
    if (profiler::AdvancedTracer::getInstance().isTracing()) {                                     \
        profiler::AdvancedTracer::getInstance().traceException(type, message);                     \
    }

} // namespace profiler

#else

#define ADVANCED_TRACE_START(config)
#define ADVANCED_TRACE_STOP()
#define ADVANCED_TRACE_REPORT(output_file)
#define ADVANCED_TRACE_FUNCTION()
#define ADVANCED_TRACE_MEMORY_ALLOC(ptr, size, type)
#define ADVANCED_TRACE_MEMORY_FREE(ptr)
#define ADVANCED_TRACE_FILE_OP(operation, filename, bytes, duration_us)
#define ADVANCED_TRACE_EXCEPTION(type, message)

#endif
