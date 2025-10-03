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
        uint64_t call_count = 0;
        uint64_t total_time_us = 0;
        uint64_t min_time = UINT64_MAX;
        uint64_t max_time = 0;
    };

    struct MemoryStats {
        size_t allocations = 0;
        size_t deallocations = 0;
        size_t total_allocated = 0;
        size_t total_deallocated = 0;
        size_t max_allocation = 0;
        size_t current_usage = 0;
        size_t peak_usage = 0;
    };

    struct MemoryTypeStats {
        size_t count = 0;
        size_t total_size = 0;
    };

    struct IOStats {
        std::string filename;
        std::unordered_map<std::string, size_t> operations;
        size_t total_bytes = 0;
        uint64_t total_time_us = 0;
        uint64_t min_time = UINT64_MAX;
        uint64_t max_time = 0;
    };

    struct NetworkStats {
        std::string endpoint;
        std::unordered_map<std::string, size_t> operations;
        size_t total_bytes = 0;
        uint64_t total_time_us = 0;
        uint64_t min_time = UINT64_MAX;
        uint64_t max_time = 0;
    };

    struct PerformanceMetric {
        std::string name;
        std::vector<double> values;
        double min_value = 0;
        double max_value = 0;
    };

  public:
    static Statistics &getInstance();

    // 函数调用统计
    void recordFunctionCall(const std::string &func_name, uint64_t duration_us);
    void recordFunctionCallWithArgs(
        const std::string &func_name, uint64_t duration_us, const std::vector<std::string> &args);

    // 内存统计
    void recordMemoryAllocation(size_t size, const std::string &type = "");
    void recordMemoryDeallocation(size_t size, const std::string &type = "");
    void recordMemoryUsage(size_t current, size_t peak);

    // IO统计
    void recordFileOperation(
        const std::string &operation, const std::string &filename, size_t bytes,
        uint64_t duration_us);

    // 网络统计
    void recordNetworkOperation(
        const std::string &operation, const std::string &endpoint, size_t bytes,
        uint64_t duration_us);

    // 异常统计
    void recordException(const std::string &exception_type, const std::string &message);
    void recordExceptionHandled(const std::string &exception_type);

    // 性能指标
    void recordPerformanceMetric(const std::string &metric_name, double value);

    // 计数器
    void recordCounter(const std::string &counter_name, int64_t value);

    // 报告生成
    // 报告生成
    void generateSummaryReport(
        const std::string &output_file = "profile_reports/statistics_summary.json");
    void generateDetailedReport(
        const std::string &output_file = "profile_reports/statistics_detailed.json");
    void generateCallGraph(const std::string &output_file = "profile_reports/call_graph.json");

    // 配置
    void setSamplingRate(double rate);
    void setMaxSamples(size_t max_samples);
    void enableRealTimeAnalysis(bool enable);

    // 获取统计信息
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
    Statistics() = default;
    ~Statistics() = default;

    mutable std::mutex mutex_;

    std::unordered_map<std::string, FunctionStats> function_stats_;
    std::unordered_map<std::string, std::vector<std::string>> function_args_;
    uint64_t total_function_calls_ = 0;
    uint64_t total_function_time_ = 0;

    MemoryStats memory_stats_;
    std::unordered_map<std::string, MemoryTypeStats> memory_type_stats_;
    size_t current_memory_usage_ = 0;
    size_t peak_memory_usage_ = 0;

    std::unordered_map<std::string, IOStats> io_stats_;
    size_t total_io_operations_ = 0;
    size_t total_io_bytes_ = 0;
    uint64_t total_io_time_us_ = 0;

    std::unordered_map<std::string, NetworkStats> network_stats_;
    size_t total_network_operations_ = 0;
    size_t total_network_bytes_ = 0;
    uint64_t total_network_time_us_ = 0;

    std::unordered_map<std::string, size_t> exception_stats_;
    std::unordered_map<std::string, size_t> handled_exceptions_;
    std::unordered_map<std::string, std::set<std::string>> exception_messages_;
    size_t total_exceptions_ = 0;

    std::unordered_map<std::string, PerformanceMetric> performance_metrics_;

    std::unordered_map<std::string, int64_t> counters_;

    double sampling_rate_ = 1.0;
    size_t max_samples_ = 10000;
    bool real_time_analysis_ = false;
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
