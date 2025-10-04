#include "statistics.h"
#include "../core/trace.h"
#include "../tracers/call_tracer.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numeric>
#include <sstream>

#ifndef NDEBUG

namespace profiler {

Statistics &Statistics::getInstance() {
    static Statistics instance;
    return instance;
}

void Statistics::recordFunctionCall(const std::string &func_name, uint64_t duration_us) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto &stats = functionStats_[func_name];
    stats.function_name = func_name;
    stats.callCount++;
    stats.totalTimeUs += duration_us;

    if (duration_us < stats.minTime)
        stats.minTime = duration_us;
    if (duration_us > stats.maxTime)
        stats.maxTime = duration_us;

    totalFunctionCalls_++;
    totalFunctionTime_ += duration_us;
}

void Statistics::recordFunctionCallWithArgs(
    const std::string &func_name, uint64_t duration_us, const std::vector<std::string> &args) {
    recordFunctionCall(func_name, duration_us);

    std::lock_guard<std::mutex> lock(mutex_);
    functionArgs_[func_name] = args;
}

void Statistics::recordMemoryAllocation(size_t size, const std::string &type) {
    std::lock_guard<std::mutex> lock(mutex_);

    memory_stats_.allocations++;
    memory_stats_.totalAllocated += size;
    if (size > memory_stats_.maxAllocation)
        memory_stats_.maxAllocation = size;

    if (!type.empty()) {
        memoryTypeStats_[type].count++;
        memoryTypeStats_[type].totalSize += size;
    }

    currentMemoryUsage_ += size;
    if (currentMemoryUsage_ > peakMemoryUsage_) {
        peakMemoryUsage_ = currentMemoryUsage_;
    }
}

void Statistics::recordMemoryDeallocation(size_t size, const std::string &type) {
    std::lock_guard<std::mutex> lock(mutex_);

    memory_stats_.deallocations++;
    memory_stats_.totalDeallocated += size;

    if (!type.empty()) {
        memoryTypeStats_[type].count--;
        memoryTypeStats_[type].totalSize -= size;
    }

    if (currentMemoryUsage_ >= size) {
        currentMemoryUsage_ -= size;
    }
}

void Statistics::recordMemoryUsage(size_t current, size_t peak) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentMemoryUsage_ = current;
    if (peak > peakMemoryUsage_)
        peakMemoryUsage_ = peak;
}

void Statistics::recordFileOperation(
    const std::string &operation, const std::string &filename, size_t bytes, uint64_t duration_us) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto &stats = io_stats_[filename];
    stats.filename = filename;
    stats.operations[operation]++;
    stats.totalBytes += bytes;
    stats.totalTimeUs += duration_us;

    if (duration_us < stats.minTime)
        stats.minTime = duration_us;
    if (duration_us > stats.maxTime)
        stats.maxTime = duration_us;

    totalIoOperations_++;
    totalIoBytes_ += bytes;
    totalIoTimeUs_ += duration_us;
}

void Statistics::recordNetworkOperation(
    const std::string &operation, const std::string &endpoint, size_t bytes, uint64_t duration_us) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto &stats = networkStats_[endpoint];
    stats.endpoint = endpoint;
    stats.operations[operation]++;
    stats.totalBytes += bytes;
    stats.totalTimeUs += duration_us;

    if (duration_us < stats.minTime)
        stats.minTime = duration_us;
    if (duration_us > stats.maxTime)
        stats.maxTime = duration_us;

    totalNetworkOperations_++;
    totalNetworkBytes_ += bytes;
    totalNetworkTimeUs_ += duration_us;
}

void Statistics::recordException(const std::string &exception_type, const std::string &message) {
    std::lock_guard<std::mutex> lock(mutex_);

    exceptionStats_[exception_type]++;
    exceptionMessages_[exception_type].insert(message);
    totalExceptions_++;
}

void Statistics::recordExceptionHandled(const std::string &exception_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    handledExceptions_[exception_type]++;
}

void Statistics::recordPerformanceMetric(const std::string &metric_name, double value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto &metric = performanceMetrics_[metric_name];
    metric.name = metric_name;
    metric.values.push_back(value);
    if (metric.values.size() == 1) {
        metric.minValue = value;
        metric.maxValue = value;
    } else {
        if (value < metric.minValue)
            metric.minValue = value;
        if (value > metric.maxValue)
            metric.maxValue = value;
    }
}

void Statistics::recordCounter(const std::string &counter_name, int64_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    counters_[counter_name] += value;
}

void Statistics::generateSummaryReport(const std::string &output_file) {
    std::string final_output_file =
        output_file.empty() ? "profile_reports/statistics_summary.json" : output_file;
    std::filesystem::path filepath(final_output_file);
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path());
    }

    std::ofstream file(final_output_file);
    if (file.is_open()) {
        nlohmann::json report;

        report["total_function_calls"] = totalFunctionCalls_;
        report["total_function_time_us"] = totalFunctionTime_;
        report["total_io_operations"] = totalIoOperations_;
        report["total_io_bytes"] = totalIoBytes_;
        report["total_io_time_us"] = totalIoTimeUs_;
        report["total_network_operations"] = totalNetworkOperations_;
        report["total_network_bytes"] = totalNetworkBytes_;
        report["total_network_time_us"] = totalNetworkTimeUs_;
        report["total_exceptions"] = totalExceptions_;
        report["current_memory_usage"] = currentMemoryUsage_;
        report["peak_memory_usage"] = peakMemoryUsage_;

        nlohmann::json memory_data;
        memory_data["allocations"] = memory_stats_.allocations;
        memory_data["deallocations"] = memory_stats_.deallocations;
        memory_data["total_allocated"] = memory_stats_.totalAllocated;
        memory_data["total_deallocated"] = memory_stats_.totalDeallocated;
        memory_data["max_allocation"] = memory_stats_.maxAllocation;
        memory_data["current_usage"] = memory_stats_.currentUsage;
        memory_data["peak_usage"] = memory_stats_.peakUsage;
        report["memory_stats"] = memory_data;

        nlohmann::json memory_type_data = nlohmann::json::object();
        for (const auto &pair : memoryTypeStats_) {
            const auto &type = pair.first;
            const auto &stats = pair.second;
            nlohmann::json type_data;
            type_data["count"] = stats.count;
            type_data["total_size"] = stats.totalSize;
            memory_type_data[type] = type_data;
        }
        report["memory_type_stats"] = memory_type_data;

        nlohmann::json exception_data = nlohmann::json::object();
        for (const auto &pair : exceptionStats_) {
            exception_data[pair.first] = pair.second;
        }
        report["exception_stats"] = exception_data;

        nlohmann::json handled_exception_data = nlohmann::json::object();
        for (const auto &pair : handledExceptions_) {
            handled_exception_data[pair.first] = pair.second;
        }
        report["handled_exceptions"] = handled_exception_data;

        nlohmann::json metrics_data = nlohmann::json::object();
        for (const auto &pair : performanceMetrics_) {
            const auto &name = pair.first;
            const auto &metric = pair.second;
            nlohmann::json metric_data;
            metric_data["min_value"] = metric.minValue;
            metric_data["max_value"] = metric.maxValue;
            metric_data["values"] = metric.values;
            metrics_data[name] = metric_data;
        }
        report["performance_metrics"] = metrics_data;

        file << std::setw(2) << report << std::endl;
        file.close();
        // std::cout << "[PROFILER STATISTICS] Summary report generated: " << final_output_file <<
        // std::endl;
    }
}

void Statistics::generateDetailedReport(const std::string &output_file) {
    std::string final_output_file =
        output_file.empty() ? "profile_reports/statistics_detailed.json" : output_file;
    std::filesystem::path filepath(final_output_file);
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path());
    }

    std::ofstream file(final_output_file);
    if (file.is_open()) {
        nlohmann::json report;

        nlohmann::json function_stats = nlohmann::json::object();
        for (const auto &pair : functionStats_) {
            const auto &name = pair.first;
            const auto &stats = pair.second;

            nlohmann::json func_data;
            func_data["call_count"] = stats.callCount;
            func_data["total_time_us"] = stats.totalTimeUs;
            func_data["min_time_us"] = stats.minTime;
            func_data["max_time_us"] = stats.maxTime;
            func_data["avg_time_us"] =
                stats.callCount > 0 ? stats.totalTimeUs / stats.callCount : 0;

            function_stats[name] = func_data;
        }
        report["function_stats"] = function_stats;

        nlohmann::json io_stats = nlohmann::json::object();
        for (const auto &pair : io_stats_) {
            const auto &filename = pair.first;
            const auto &stats = pair.second;

            nlohmann::json file_data;
            file_data["total_bytes"] = stats.totalBytes;
            file_data["total_time_us"] = stats.totalTimeUs;
            file_data["min_time_us"] = stats.minTime;
            file_data["max_time_us"] = stats.maxTime;

            nlohmann::json operations = nlohmann::json::object();
            for (const auto &op_pair : stats.operations) {
                operations[op_pair.first] = op_pair.second;
            }
            file_data["operations"] = operations;

            io_stats[filename] = file_data;
        }
        report["io_stats"] = io_stats;

        nlohmann::json network_stats = nlohmann::json::object();
        for (const auto &pair : networkStats_) {
            const auto &endpoint = pair.first;
            const auto &stats = pair.second;

            nlohmann::json endpoint_data;
            endpoint_data["total_bytes"] = stats.totalBytes;
            endpoint_data["total_time_us"] = stats.totalTimeUs;
            endpoint_data["min_time_us"] = stats.minTime;
            endpoint_data["max_time_us"] = stats.maxTime;

            nlohmann::json operations = nlohmann::json::object();
            for (const auto &op_pair : stats.operations) {
                operations[op_pair.first] = op_pair.second;
            }
            endpoint_data["operations"] = operations;

            network_stats[endpoint] = endpoint_data;
        }
        report["network_stats"] = network_stats;

        nlohmann::json memory_stats;
        memory_stats["allocations"] = memory_stats_.allocations;
        memory_stats["deallocations"] = memory_stats_.deallocations;
        memory_stats["total_allocated"] = memory_stats_.totalAllocated;
        memory_stats["total_deallocated"] = memory_stats_.totalDeallocated;
        memory_stats["max_allocation"] = memory_stats_.maxAllocation;
        memory_stats["current_usage"] = memory_stats_.currentUsage;
        memory_stats["peak_usage"] = memory_stats_.peakUsage;
        report["memory_stats"] = memory_stats;

        nlohmann::json memory_type_stats = nlohmann::json::object();
        for (const auto &pair : memoryTypeStats_) {
            const auto &type = pair.first;
            const auto &stats = pair.second;
            nlohmann::json type_data;
            type_data["count"] = stats.count;
            type_data["total_size"] = stats.totalSize;
            memory_type_stats[type] = type_data;
        }
        report["memory_type_stats"] = memory_type_stats;

        nlohmann::json exception_stats = nlohmann::json::object();
        for (const auto &pair : exceptionStats_) {
            exception_stats[pair.first] = pair.second;
        }
        report["exception_stats"] = exception_stats;

        nlohmann::json handled_exceptions = nlohmann::json::object();
        for (const auto &pair : handledExceptions_) {
            handled_exceptions[pair.first] = pair.second;
        }
        report["handled_exceptions"] = handled_exceptions;

        nlohmann::json performance_metrics = nlohmann::json::object();
        for (const auto &pair : performanceMetrics_) {
            const auto &name = pair.first;
            const auto &metric = pair.second;
            nlohmann::json metric_data;
            metric_data["min_value"] = metric.minValue;
            metric_data["max_value"] = metric.maxValue;
            metric_data["values"] = metric.values;
            performance_metrics[name] = metric_data;
        }
        report["performance_metrics"] = performance_metrics;

        file << std::setw(2) << report << std::endl;
        file.close();
        // std::cout << "[PROFILER STATISTICS] Detailed report generated: " << final_output_file <<
        // std::endl;
    }
}

void Statistics::generateCallGraph(const std::string &output_file) {
    std::string final_output_file =
        output_file.empty() ? "profile_reports/call_graph.json" : output_file;
    std::filesystem::path filepath(final_output_file);
    if (filepath.has_parent_path()) {
        std::filesystem::create_directories(filepath.parent_path());
    }

    nlohmann::json call_graph = nlohmann::json::object();

    for (const auto &pair : functionStats_) {
        const auto &stats = pair.second;

        nlohmann::json node;
        node["call_count"] = stats.callCount;
        node["total_time_us"] = stats.totalTimeUs;
        node["min_time_us"] = stats.minTime;
        node["max_time_us"] = stats.maxTime;
        node["avg_time_us"] = stats.callCount > 0 ? stats.totalTimeUs / stats.callCount : 0;

        /*if (!stats.callees.empty()) {
            nlohmann::json callees = nlohmann::json::array();
            for (const auto &callee : stats.callees) {
                callees.push_back(callee);
            }
            node["callees"] = callees;
        }*/

        call_graph[stats.function_name] = node;
    }

    std::ofstream file(final_output_file);
    if (file.is_open()) {
        file << std::setw(2) << call_graph << std::endl;
        file.close();
        // std::cout << "[PROFILER STATISTICS] Call graph generated: " << final_output_file <<
        // std::endl;
    }
}

void Statistics::setSamplingRate(double rate) {
    std::lock_guard<std::mutex> lock(mutex_);
    samplingRate_ = std::min(1.0, std::max(0.0, rate));
}

void Statistics::setMaxSamples(size_t maxSamples) {
    std::lock_guard<std::mutex> lock(mutex_);
    maxSamples_ = maxSamples;
}

void Statistics::enableRealTimeAnalysis(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    realTimeAnalysis_ = enable;
}

const std::unordered_map<std::string, Statistics::FunctionStats> &
Statistics::getFunctionStats() const {
    return functionStats_;
}

const Statistics::MemoryStats &Statistics::getMemoryStats() const { return memory_stats_; }

const std::unordered_map<std::string, Statistics::IOStats> &Statistics::getIOStats() const {
    return io_stats_;
}

const std::unordered_map<std::string, Statistics::NetworkStats> &
Statistics::getNetworkStats() const {
    return networkStats_;
}

const std::unordered_map<std::string, size_t> &Statistics::getExceptionStats() const {
    return exceptionStats_;
}

const std::unordered_map<std::string, Statistics::PerformanceMetric> &
Statistics::getPerformanceMetrics() const {
    return performanceMetrics_;
}

uint64_t Statistics::getTotalFunctionCalls() const { return totalFunctionCalls_; }

uint64_t Statistics::getTotalFunctionTime() const { return totalFunctionTime_; }

size_t Statistics::getTotalIOOperations() const { return totalIoOperations_; }

size_t Statistics::getTotalIOBytes() const { return totalIoBytes_; }

uint64_t Statistics::getTotalIOTime() const { return totalIoTimeUs_; }

size_t Statistics::getTotalNetworkOperations() const { return totalNetworkOperations_; }

size_t Statistics::getTotalNetworkBytes() const { return totalNetworkBytes_; }

uint64_t Statistics::getTotalNetworkTime() const { return totalNetworkTimeUs_; }

size_t Statistics::getTotalExceptions() const { return totalExceptions_; }

size_t Statistics::getCurrentMemoryUsage() const { return currentMemoryUsage_; }

size_t Statistics::getPeakMemoryUsage() const { return peakMemoryUsage_; }

} // namespace profiler

#endif // NDEBUG