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

    auto &stats = function_stats_[func_name];
    stats.function_name = func_name;
    stats.call_count++;
    stats.total_time_us += duration_us;

    if (duration_us < stats.min_time)
        stats.min_time = duration_us;
    if (duration_us > stats.max_time)
        stats.max_time = duration_us;

    total_function_calls_++;
    total_function_time_ += duration_us;
}

void Statistics::recordFunctionCallWithArgs(
    const std::string &func_name, uint64_t duration_us, const std::vector<std::string> &args) {
    recordFunctionCall(func_name, duration_us);

    std::lock_guard<std::mutex> lock(mutex_);
    function_args_[func_name] = args;
}

void Statistics::recordMemoryAllocation(size_t size, const std::string &type) {
    std::lock_guard<std::mutex> lock(mutex_);

    memory_stats_.allocations++;
    memory_stats_.total_allocated += size;
    if (size > memory_stats_.max_allocation)
        memory_stats_.max_allocation = size;

    if (!type.empty()) {
        memory_type_stats_[type].count++;
        memory_type_stats_[type].total_size += size;
    }

    current_memory_usage_ += size;
    if (current_memory_usage_ > peak_memory_usage_) {
        peak_memory_usage_ = current_memory_usage_;
    }
}

void Statistics::recordMemoryDeallocation(size_t size, const std::string &type) {
    std::lock_guard<std::mutex> lock(mutex_);

    memory_stats_.deallocations++;
    memory_stats_.total_deallocated += size;

    if (!type.empty()) {
        memory_type_stats_[type].count--;
        memory_type_stats_[type].total_size -= size;
    }

    if (current_memory_usage_ >= size) {
        current_memory_usage_ -= size;
    }
}

void Statistics::recordMemoryUsage(size_t current, size_t peak) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_memory_usage_ = current;
    if (peak > peak_memory_usage_)
        peak_memory_usage_ = peak;
}

void Statistics::recordFileOperation(
    const std::string &operation, const std::string &filename, size_t bytes, uint64_t duration_us) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto &stats = io_stats_[filename];
    stats.filename = filename;
    stats.operations[operation]++;
    stats.total_bytes += bytes;
    stats.total_time_us += duration_us;

    if (duration_us < stats.min_time)
        stats.min_time = duration_us;
    if (duration_us > stats.max_time)
        stats.max_time = duration_us;

    total_io_operations_++;
    total_io_bytes_ += bytes;
    total_io_time_us_ += duration_us;
}

void Statistics::recordNetworkOperation(
    const std::string &operation, const std::string &endpoint, size_t bytes, uint64_t duration_us) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto &stats = network_stats_[endpoint];
    stats.endpoint = endpoint;
    stats.operations[operation]++;
    stats.total_bytes += bytes;
    stats.total_time_us += duration_us;

    if (duration_us < stats.min_time)
        stats.min_time = duration_us;
    if (duration_us > stats.max_time)
        stats.max_time = duration_us;

    total_network_operations_++;
    total_network_bytes_ += bytes;
    total_network_time_us_ += duration_us;
}

void Statistics::recordException(const std::string &exception_type, const std::string &message) {
    std::lock_guard<std::mutex> lock(mutex_);

    exception_stats_[exception_type]++;
    exception_messages_[exception_type].insert(message);
    total_exceptions_++;
}

void Statistics::recordExceptionHandled(const std::string &exception_type) {
    std::lock_guard<std::mutex> lock(mutex_);
    handled_exceptions_[exception_type]++;
}

void Statistics::recordPerformanceMetric(const std::string &metric_name, double value) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto &metric = performance_metrics_[metric_name];
    metric.name = metric_name;
    metric.values.push_back(value);
    if (metric.values.size() == 1) {
        metric.min_value = value;
        metric.max_value = value;
    } else {
        if (value < metric.min_value)
            metric.min_value = value;
        if (value > metric.max_value)
            metric.max_value = value;
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

        report["total_function_calls"] = total_function_calls_;
        report["total_function_time_us"] = total_function_time_;
        report["total_io_operations"] = total_io_operations_;
        report["total_io_bytes"] = total_io_bytes_;
        report["total_io_time_us"] = total_io_time_us_;
        report["total_network_operations"] = total_network_operations_;
        report["total_network_bytes"] = total_network_bytes_;
        report["total_network_time_us"] = total_network_time_us_;
        report["total_exceptions"] = total_exceptions_;
        report["current_memory_usage"] = current_memory_usage_;
        report["peak_memory_usage"] = peak_memory_usage_;

        nlohmann::json memory_data;
        memory_data["allocations"] = memory_stats_.allocations;
        memory_data["deallocations"] = memory_stats_.deallocations;
        memory_data["total_allocated"] = memory_stats_.total_allocated;
        memory_data["total_deallocated"] = memory_stats_.total_deallocated;
        memory_data["max_allocation"] = memory_stats_.max_allocation;
        memory_data["current_usage"] = memory_stats_.current_usage;
        memory_data["peak_usage"] = memory_stats_.peak_usage;
        report["memory_stats"] = memory_data;

        nlohmann::json memory_type_data = nlohmann::json::object();
        for (const auto &pair : memory_type_stats_) {
            const auto &type = pair.first;
            const auto &stats = pair.second;
            nlohmann::json type_data;
            type_data["count"] = stats.count;
            type_data["total_size"] = stats.total_size;
            memory_type_data[type] = type_data;
        }
        report["memory_type_stats"] = memory_type_data;

        nlohmann::json exception_data = nlohmann::json::object();
        for (const auto &pair : exception_stats_) {
            exception_data[pair.first] = pair.second;
        }
        report["exception_stats"] = exception_data;

        nlohmann::json handled_exception_data = nlohmann::json::object();
        for (const auto &pair : handled_exceptions_) {
            handled_exception_data[pair.first] = pair.second;
        }
        report["handled_exceptions"] = handled_exception_data;

        nlohmann::json metrics_data = nlohmann::json::object();
        for (const auto &pair : performance_metrics_) {
            const auto &name = pair.first;
            const auto &metric = pair.second;
            nlohmann::json metric_data;
            metric_data["min_value"] = metric.min_value;
            metric_data["max_value"] = metric.max_value;
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
        for (const auto &pair : function_stats_) {
            const auto &name = pair.first;
            const auto &stats = pair.second;

            nlohmann::json func_data;
            func_data["call_count"] = stats.call_count;
            func_data["total_time_us"] = stats.total_time_us;
            func_data["min_time_us"] = stats.min_time;
            func_data["max_time_us"] = stats.max_time;
            func_data["avg_time_us"] =
                stats.call_count > 0 ? stats.total_time_us / stats.call_count : 0;

            function_stats[name] = func_data;
        }
        report["function_stats"] = function_stats;

        nlohmann::json io_stats = nlohmann::json::object();
        for (const auto &pair : io_stats_) {
            const auto &filename = pair.first;
            const auto &stats = pair.second;

            nlohmann::json file_data;
            file_data["total_bytes"] = stats.total_bytes;
            file_data["total_time_us"] = stats.total_time_us;
            file_data["min_time_us"] = stats.min_time;
            file_data["max_time_us"] = stats.max_time;

            nlohmann::json operations = nlohmann::json::object();
            for (const auto &op_pair : stats.operations) {
                operations[op_pair.first] = op_pair.second;
            }
            file_data["operations"] = operations;

            io_stats[filename] = file_data;
        }
        report["io_stats"] = io_stats;

        nlohmann::json network_stats = nlohmann::json::object();
        for (const auto &pair : network_stats_) {
            const auto &endpoint = pair.first;
            const auto &stats = pair.second;

            nlohmann::json endpoint_data;
            endpoint_data["total_bytes"] = stats.total_bytes;
            endpoint_data["total_time_us"] = stats.total_time_us;
            endpoint_data["min_time_us"] = stats.min_time;
            endpoint_data["max_time_us"] = stats.max_time;

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
        memory_stats["total_allocated"] = memory_stats_.total_allocated;
        memory_stats["total_deallocated"] = memory_stats_.total_deallocated;
        memory_stats["max_allocation"] = memory_stats_.max_allocation;
        memory_stats["current_usage"] = memory_stats_.current_usage;
        memory_stats["peak_usage"] = memory_stats_.peak_usage;
        report["memory_stats"] = memory_stats;

        nlohmann::json memory_type_stats = nlohmann::json::object();
        for (const auto &pair : memory_type_stats_) {
            const auto &type = pair.first;
            const auto &stats = pair.second;
            nlohmann::json type_data;
            type_data["count"] = stats.count;
            type_data["total_size"] = stats.total_size;
            memory_type_stats[type] = type_data;
        }
        report["memory_type_stats"] = memory_type_stats;

        nlohmann::json exception_stats = nlohmann::json::object();
        for (const auto &pair : exception_stats_) {
            exception_stats[pair.first] = pair.second;
        }
        report["exception_stats"] = exception_stats;

        nlohmann::json handled_exceptions = nlohmann::json::object();
        for (const auto &pair : handled_exceptions_) {
            handled_exceptions[pair.first] = pair.second;
        }
        report["handled_exceptions"] = handled_exceptions;

        nlohmann::json performance_metrics = nlohmann::json::object();
        for (const auto &pair : performance_metrics_) {
            const auto &name = pair.first;
            const auto &metric = pair.second;
            nlohmann::json metric_data;
            metric_data["min_value"] = metric.min_value;
            metric_data["max_value"] = metric.max_value;
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

    for (const auto &pair : function_stats_) {
        const auto &stats = pair.second;

        nlohmann::json node;
        node["call_count"] = stats.call_count;
        node["total_time_us"] = stats.total_time_us;
        node["min_time_us"] = stats.min_time;
        node["max_time_us"] = stats.max_time;
        node["avg_time_us"] = stats.call_count > 0 ? stats.total_time_us / stats.call_count : 0;

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
    sampling_rate_ = std::min(1.0, std::max(0.0, rate));
}

void Statistics::setMaxSamples(size_t max_samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_samples_ = max_samples;
}

void Statistics::enableRealTimeAnalysis(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    real_time_analysis_ = enable;
}

const std::unordered_map<std::string, Statistics::FunctionStats> &
Statistics::getFunctionStats() const {
    return function_stats_;
}

const Statistics::MemoryStats &Statistics::getMemoryStats() const { return memory_stats_; }

const std::unordered_map<std::string, Statistics::IOStats> &Statistics::getIOStats() const {
    return io_stats_;
}

const std::unordered_map<std::string, Statistics::NetworkStats> &
Statistics::getNetworkStats() const {
    return network_stats_;
}

const std::unordered_map<std::string, size_t> &Statistics::getExceptionStats() const {
    return exception_stats_;
}

const std::unordered_map<std::string, Statistics::PerformanceMetric> &
Statistics::getPerformanceMetrics() const {
    return performance_metrics_;
}

uint64_t Statistics::getTotalFunctionCalls() const { return total_function_calls_; }

uint64_t Statistics::getTotalFunctionTime() const { return total_function_time_; }

size_t Statistics::getTotalIOOperations() const { return total_io_operations_; }

size_t Statistics::getTotalIOBytes() const { return total_io_bytes_; }

uint64_t Statistics::getTotalIOTime() const { return total_io_time_us_; }

size_t Statistics::getTotalNetworkOperations() const { return total_network_operations_; }

size_t Statistics::getTotalNetworkBytes() const { return total_network_bytes_; }

uint64_t Statistics::getTotalNetworkTime() const { return total_network_time_us_; }

size_t Statistics::getTotalExceptions() const { return total_exceptions_; }

size_t Statistics::getCurrentMemoryUsage() const { return current_memory_usage_; }

size_t Statistics::getPeakMemoryUsage() const { return peak_memory_usage_; }

} // namespace profiler

#endif // NDEBUG