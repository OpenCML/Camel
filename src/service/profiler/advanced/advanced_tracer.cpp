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
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "advanced_tracer.h"
#include "../analysis/perfetto_integration.h"
#include "../core/trace.h"
#include "scope_guard.h"
#include "utils/log.h"

#include <atomic>
#include <iostream>

#ifndef NDEBUG

namespace profiler {

static std::atomic<bool> g_profiling_enabled(false);
static std::atomic<bool> g_profiling_stopped(false);

class ProfilingCleanup {
  public:
    ~ProfilingCleanup() {
        if (g_profiling_enabled && !g_profiling_stopped) {
            l.in("Profiler")
                .warn("Profiling was enabled but not properly stopped with enable(false). "
                      "Performance data may not have been saved.");
        }
    }
};

static ProfilingCleanup g_cleanup;

using namespace std::chrono;

AdvancedTracer &AdvancedTracer::getInstance() {
    static AdvancedTracer instance;
    return instance;
}

void AdvancedTracer::initialize(const Config &config) {
    config_ = config;

    if (config_.enablePerfettoIntegration) {
        perfettoIntegration_ = std::make_unique<PerfettoIntegration>();
        perfettoIntegration_->startTracing(config_.perfettoOutput);
    }
}

void AdvancedTracer::initializeWithDefaults() {
    Config default_config;
    initialize(default_config);
}

void AdvancedTracer::startTracing() {
    if (tracingEnabled_)
        return;
    g_profiling_enabled = true;
    g_profiling_stopped = false;
    tracingEnabled_     = true;
    tracingPaused_      = false;

    traceStartTime_  = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    traceEventCount_ = 0;

    CallTracer::getInstance().startTracing();

    if (perfettoIntegration_) {
        perfettoIntegration_->startTracing(config_.perfettoOutput);
    }

    TRACE_EVENT_INSTANT("advanced_tracing_started");
}

void AdvancedTracer::stopTracing() {
    if (!tracingEnabled_) {
        return;
    }

    tracingEnabled_     = false;
    g_profiling_stopped = true;
    traceEndTime_ = duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();

    CallTracer::getInstance().stopTracing();

    if (perfettoIntegration_) {
        perfettoIntegration_->stopAndOpenPerfetto();
    }

    TRACE_EVENT_INSTANT("advanced_tracing_stopped");

    if (config_.generateSummaryReport) {
        generateStatisticsReport();
    }

    if (config_.generateCallGraph) {
        generateCallGraph();
    }

    generateTraceFile();
}

void AdvancedTracer::pauseTracing() {
    if (!tracingEnabled_ || tracingPaused_)
        return;

    tracingPaused_ = true;
    TRACE_EVENT_INSTANT("advanced_tracing_paused");
}

void AdvancedTracer::resumeTracing() {
    if (!tracingEnabled_ || !tracingPaused_)
        return;

    tracingPaused_ = false;
    TRACE_EVENT_INSTANT("advanced_tracing_resumed");
}

bool AdvancedTracer::isTracing() const { return tracingEnabled_ && !tracingPaused_; }

void AdvancedTracer::traceFunctionCall(
    const std::string &func_name, const std::string &file, int line) {
    if (!isTracing() || !shouldTraceFunction(func_name)) {
        return;
    }

    CallTracer::getInstance().functionEnter(func_name, file, line);

    if (perfettoIntegration_) {
        perfettoIntegration_->recordEventBegin(func_name, "function");
    }

    traceEventCount_++;

    Statistics::getInstance().recordFunctionCall(func_name, 0); // Duration recorded on return
}

void AdvancedTracer::traceFunctionReturn(const std::string &func_name) {
    if (!isTracing() || !shouldTraceFunction(func_name)) {
        return;
    }

    CallTracer::getInstance().functionExit(func_name);

    if (perfettoIntegration_) {
        perfettoIntegration_->recordEventEnd(func_name, "function");
    }

    traceEventCount_++;
}
void AdvancedTracer::traceMemoryAllocation(void *ptr, size_t size, const std::string &type) {
    if (!isTracing())
        return;

    CallTracer::getInstance().memoryAllocate(ptr, size, type);
    Statistics::getInstance().recordMemoryAllocation(size, type);

    if (perfettoIntegration_) {
        perfettoIntegration_->recordEventInstant("memory_alloc:" + std::to_string(size), "memory");
    }
}

void AdvancedTracer::traceMemoryDeallocation(void *ptr) {
    if (!isTracing())
        return;

    CallTracer::getInstance().memoryDeallocate(ptr);

    if (perfettoIntegration_) {
        perfettoIntegration_->recordEventInstant("memory_free", "memory");
    }
}

void AdvancedTracer::traceFileOperation(
    const std::string &operation, const std::string &filename, size_t bytes, uint64_t duration_us) {
    if (!isTracing())
        return;

    if (operation == "open") {
        CallTracer::getInstance().fileOpen(filename, "r");
    } else if (operation == "close") {
        CallTracer::getInstance().fileClose(filename);
    } else if (operation == "read") {
        CallTracer::getInstance().fileRead(filename, bytes);
    } else if (operation == "write") {
        CallTracer::getInstance().fileWrite(filename, bytes);
    }

    Statistics::getInstance().recordFileOperation(operation, filename, bytes, duration_us);

    if (perfettoIntegration_) {
        perfettoIntegration_->recordEventInstant("file_" + operation + ":" + filename, "io");
    }
}

void AdvancedTracer::traceNetworkOperation(
    const std::string &operation, const std::string &endpoint, size_t bytes, uint64_t duration_us) {
    if (!isTracing())
        return;

    Statistics::getInstance().recordNetworkOperation(operation, endpoint, bytes, duration_us);

    if (perfettoIntegration_) {
        perfettoIntegration_->recordEventInstant(operation + ":" + endpoint, "network");
    }
}

void AdvancedTracer::traceException(const std::string &exception_type, const std::string &message) {
    if (!isTracing())
        return;

    CallTracer::getInstance().exceptionThrown(exception_type, message);
    Statistics::getInstance().recordException(exception_type, message);

    if (perfettoIntegration_) {
        perfettoIntegration_->recordEventInstant("exception:" + exception_type, "exception");
    }
}

void AdvancedTracer::traceExceptionHandled(const std::string &exception_type) {
    if (!isTracing())
        return;

    CallTracer::getInstance().exceptionCaught(exception_type);
    Statistics::getInstance().recordExceptionHandled(exception_type);

    if (perfettoIntegration_) {
        perfettoIntegration_->recordEventInstant(
            "exception_handled:" + exception_type,
            "exception");
    }
}

void AdvancedTracer::recordMetric(const std::string &name, double value) {
    if (!isTracing())
        return;

    Statistics::getInstance().recordPerformanceMetric(name, value);
}

void AdvancedTracer::incrementCounter(const std::string &name, int64_t increment) {
    if (!isTracing())
        return;

    Statistics::getInstance().recordCounter(name, increment);
}

void AdvancedTracer::generateTraceFile() {
    trace_event_flush(config_.outputFile);

    if (config_.generateSummaryReport) {
        Statistics::getInstance().generateSummaryReport(config_.outputFile);
    }
}

void AdvancedTracer::generateStatisticsReport() {
    std::filesystem::create_directories("profile_reports");
    Statistics::getInstance().generateDetailedReport("profile_reports/statistics_report.json");
}

void AdvancedTracer::generateCallGraph() {
    std::filesystem::create_directories("profile_reports");
    Statistics::getInstance().generateCallGraph("profile_reports/call_graph.json");
}

void AdvancedTracer::openPerfettoUI() {
    if (perfettoIntegration_) {
        perfettoIntegration_->openPerfettoInBrowser();
    }
}

void AdvancedTracer::enableRealTimeAnalysis(bool enable) { config_.realTimeAnalysis = enable; }

void AdvancedTracer::setSamplingRate(double rate) {
    config_.samplingRate = rate;
    Statistics::getInstance().setSamplingRate(rate);
}

void AdvancedTracer::setOutputFile(const std::string &filename) { config_.outputFile = filename; }

void AdvancedTracer::addFunctionFilter(const std::string &func_name, bool include) {
    if (include) {
        config_.includeFunctions.insert(func_name);
    } else {
        config_.excludeFunctions.insert(func_name);
    }
}

void AdvancedTracer::removeFunctionFilter(const std::string &func_name) {
    config_.includeFunctions.erase(func_name);
    config_.excludeFunctions.erase(func_name);
}

void AdvancedTracer::clearFunctionFilters() {
    config_.includeFunctions.clear();
    config_.excludeFunctions.clear();
}

const AdvancedTracer::Config &AdvancedTracer::getConfig() const { return config_; }

uint64_t AdvancedTracer::getTraceDuration() const {
    if (traceStartTime_ == 0)
        return 0;
    uint64_t end_time =
        traceEndTime_ > 0
            ? traceEndTime_
            : duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count();
    return end_time - traceStartTime_;
}

size_t AdvancedTracer::getTraceEventCount() const { return traceEventCount_; }

size_t AdvancedTracer::getMemoryUsage() const {
    return Statistics::getInstance().getCurrentMemoryUsage();
}

bool AdvancedTracer::shouldTraceFunction(const std::string &func_name) const {
    if (config_.excludeFunctions.find(func_name) != config_.excludeFunctions.end()) {
        return false;
    }

    if (!config_.includeFunctions.empty() &&
        config_.includeFunctions.find(func_name) == config_.includeFunctions.end()) {
        return false;
    }

    return true;
}

void AdvancedTracer::recordTraceEvent(
    const std::string &name, const std::string &category, const std::string &phase,
    uint64_t timestamp, uint64_t duration) {
    if (phase == "B") {
        TRACE_EVENT_BEGIN(name.c_str());
    } else if (phase == "E") {
        TRACE_EVENT_END();
    } else if (phase == "i") {
        TRACE_EVENT_INSTANT(name.c_str());
    } else if (phase == "X") {
        TRACE_EVENT_COMPLETE(name.c_str(), timestamp, duration);
    }
}

// Global function implementation
void start_advanced_tracing(const AdvancedTracer::Config &config) {
    AdvancedTracer::getInstance().initialize(config);
    AdvancedTracer::getInstance().startTracing();
}

void stop_advanced_tracing() { AdvancedTracer::getInstance().stopTracing(); }

void generate_advanced_report(const std::string &output_file) {
    if (!output_file.empty()) {
        AdvancedTracer::getInstance().setOutputFile(output_file);
    }
    AdvancedTracer::getInstance().generateTraceFile();
    AdvancedTracer::getInstance().generateStatisticsReport();
    AdvancedTracer::getInstance().generateCallGraph();

#ifdef _WIN32
    char *profile_mode = nullptr;
    size_t len         = 0;
    errno_t err        = _dupenv_s(&profile_mode, &len, "CAMEL_PROFILE_MODE");
    bool is_full_profile =
        (err == 0 && profile_mode != nullptr && std::string(profile_mode) == "FULL");
    if (profile_mode) {
        free(profile_mode);
    }
#else
    const char *profile_mode = std::getenv("CAMEL_PROFILE_MODE");
    bool is_full_profile     = (profile_mode && std::string(profile_mode) == "FULL");
#endif
    if (is_full_profile) {
        l.in("Profiler").info("Full profiling mode detected, opening Perfetto UI...");
        AdvancedTracer::getInstance().openPerfettoUI();
    }
    l.in("Profiler").info("Advanced profiling report generation completed.");
}

} // namespace profiler

#endif // NDEBUG
