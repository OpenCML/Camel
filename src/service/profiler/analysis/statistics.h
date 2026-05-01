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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#ifndef NDEBUG

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace profiler {

class Statistics {
  public:
    struct FunctionStats {
        std::string function_name;
        uint64_t callCount   = 0;
        uint64_t totalTimeUs = 0;
        uint64_t minTime     = UINT64_MAX;
        uint64_t maxTime     = 0;
    };

    struct MemoryStats {
        size_t allocations      = 0;
        size_t deallocations    = 0;
        size_t totalAllocated   = 0;
        size_t totalDeallocated = 0;
        size_t maxAllocation    = 0;
        size_t currentUsage     = 0;
        size_t peakUsage        = 0;
    };

    struct MemoryTypeStats {
        size_t count     = 0;
        size_t totalSize = 0;
    };

    struct IOStats {
        std::string filename;
        std::unordered_map<std::string, size_t> operations;
        size_t totalBytes    = 0;
        uint64_t totalTimeUs = 0;
        uint64_t minTime     = UINT64_MAX;
        uint64_t maxTime     = 0;
    };

    struct NetworkStats {
        std::string endpoint;
        std::unordered_map<std::string, size_t> operations;
        size_t totalBytes    = 0;
        uint64_t totalTimeUs = 0;
        uint64_t minTime     = UINT64_MAX;
        uint64_t maxTime     = 0;
    };

    struct PerformanceMetric {
        std::string name;
        std::vector<double> values;
        double minValue = 0;
        double maxValue = 0;
    };

  public:
    static Statistics &getInstance();

    // Function call statistics.
    void recordFunctionCall(const std::string &func_name, uint64_t duration_us);
    void recordFunctionCallWithArgs(
        const std::string &func_name, uint64_t duration_us, const std::vector<std::string> &args);

    // Memory statistics.
    void recordMemoryAllocation(size_t size, const std::string &type = "");
    void recordMemoryDeallocation(size_t size, const std::string &type = "");
    void recordMemoryUsage(size_t current, size_t peak);

    // I/O statistics.
    void recordFileOperation(
        const std::string &operation, const std::string &filename, size_t bytes,
        uint64_t duration_us);

    // Network statistics.
    void recordNetworkOperation(
        const std::string &operation, const std::string &endpoint, size_t bytes,
        uint64_t duration_us);

    // Exception statistics.
    void recordException(const std::string &exception_type, const std::string &message);
    void recordExceptionHandled(const std::string &exception_type);

    // Performance metrics.
    void recordPerformanceMetric(const std::string &metric_name, double value);

    // Counters.
    void recordCounter(const std::string &counter_name, int64_t value);

    // Report generation.
    void generateSummaryReport(
        const std::string &output_file = "profile_reports/statistics_summary.json");
    void generateDetailedReport(
        const std::string &output_file = "profile_reports/statistics_detailed.json");
    void generateCallGraph(const std::string &output_file = "profile_reports/call_graph.json");

    // Configuration.
    void setSamplingRate(double rate);
    void setMaxSamples(size_t maxSamples);
    void enableRealTimeAnalysis(bool enable);

    // Get statistics.
    const std::unordered_map<std::string, FunctionStats> &getFunctionStats() const;
    const MemoryStats &getMemoryStats() const;
    const std::unordered_map<std::string, IOStats> &getIOStats() const;
    const std::unordered_map<std::string, NetworkStats> &getNetworkStats() const;
    const std::unordered_map<std::string, size_t> &getExceptionStats() const;
    const std::unordered_map<std::string, PerformanceMetric> &getPerformanceMetrics() const;

    uint64_t getTotalFunctionCalls() const;
    uint64_t getTotalFunctionTime() const;
    size_t getTotalIOOperations() const;
    size_t getTotalIOBytes() const;
    uint64_t getTotalIOTime() const;
    size_t getTotalNetworkOperations() const;
    size_t getTotalNetworkBytes() const;
    uint64_t getTotalNetworkTime() const;
    size_t getTotalExceptions() const;
    size_t getCurrentMemoryUsage() const;
    size_t getPeakMemoryUsage() const;

  private:
    Statistics()  = default;
    ~Statistics() = default;

    mutable std::mutex mutex_;

    std::unordered_map<std::string, FunctionStats> functionStats_;
    std::unordered_map<std::string, std::vector<std::string>> functionArgs_;
    uint64_t totalFunctionCalls_ = 0;
    uint64_t totalFunctionTime_  = 0;

    MemoryStats memory_stats_;
    std::unordered_map<std::string, MemoryTypeStats> memoryTypeStats_;
    size_t currentMemoryUsage_ = 0;
    size_t peakMemoryUsage_    = 0;

    std::unordered_map<std::string, IOStats> io_stats_;
    size_t totalIoOperations_ = 0;
    size_t totalIoBytes_      = 0;
    uint64_t totalIoTimeUs_   = 0;

    std::unordered_map<std::string, NetworkStats> networkStats_;
    size_t totalNetworkOperations_ = 0;
    size_t totalNetworkBytes_      = 0;
    uint64_t totalNetworkTimeUs_   = 0;

    std::unordered_map<std::string, size_t> exceptionStats_;
    std::unordered_map<std::string, size_t> handledExceptions_;
    std::unordered_map<std::string, std::set<std::string>> exceptionMessages_;
    size_t totalExceptions_ = 0;

    std::unordered_map<std::string, PerformanceMetric> performanceMetrics_;

    std::unordered_map<std::string, int64_t> counters_;

    double samplingRate_   = 1.0;
    size_t maxSamples_     = 10000;
    bool realTimeAnalysis_ = false;
};

} // namespace profiler

#else

#define recordFunctionCall(name, duration)
#define recordMemoryAllocation(size, type)
#define recordMemoryDeallocation(size, type)
#define recordMemoryUsage(current, peak)
#define recordFileOperation(operation, filename, bytes, duration)
#define recordNetworkOperation(operation, endpoint, bytes, duration)
#define recordException(type, message)
#define recordExceptionHandled(type)
#define recordPerformanceMetric(name, value)
#define recordCounter(name, value)

#endif
