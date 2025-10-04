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
 * Updated: Oct. 04, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "call_tracer.h"
#include "../core/trace.h"

#ifndef NDEBUG
#include "../analysis/statistics.h"
#endif

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#ifndef NDEBUG
namespace profiler {

CallTracer &CallTracer::getInstance() {
    static CallTracer instance;
    return instance;
}

void CallTracer::enableFunctionTracing(bool enable) { function_tracing_enabled_ = enable; }

void CallTracer::enableMemoryTracing(bool enable) { memory_tracing_enabled_ = enable; }

void CallTracer::enableIOTracing(bool enable) { io_tracing_enabled_ = enable; }

void CallTracer::enableExceptionTracing(bool enable) { exception_tracing_enabled_ = enable; }

void CallTracer::functionEnter(const std::string &func_name, const std::string &file, int line) {
    if (!function_tracing_enabled_) {
        return;
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    TRACE_EVENT_BEGIN(func_name.c_str());

    {
        std::lock_guard<std::mutex> lock(mutex_);

        CallFrame frame;
        frame.function_name = func_name;
        frame.file = file;
        frame.line = line;
        frame.start_time = timestamp;
        frame.depth = call_stack_.size();

        call_stack_.push_back(frame);

        auto &stats = function_stats_[func_name];
        stats.function_name = func_name;
        stats.call_count++;
    }

    Statistics::getInstance().recordFunctionCall(func_name, 0);
}

void CallTracer::functionExit(const std::string &func_name) {
    if (!function_tracing_enabled_) {
        return;
    }

    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();

    TRACE_EVENT_END();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!call_stack_.empty()) {
            CallFrame frame = call_stack_.back();
            call_stack_.pop_back();

            if (frame.function_name == func_name) {
                uint64_t duration = timestamp - frame.start_time;

                auto &stats = function_stats_[func_name];
                stats.total_time += duration;
                if (duration < stats.min_time)
                    stats.min_time = duration;
                if (duration > stats.max_time)
                    stats.max_time = duration;

                Statistics::getInstance().recordFunctionCall(func_name, duration);
            }
        }
    }
}

void CallTracer::memoryAllocate(void *ptr, size_t size, const std::string &type) {
    if (!memory_tracing_enabled_)
        return;

    TRACE_EVENT_INSTANT(
        ("memory_alloc_" + std::to_string(reinterpret_cast<uintptr_t>(ptr))).c_str());

    {
        std::lock_guard<std::mutex> lock(mutex_);
        memory_allocations_[ptr] = size;
    }

    Statistics::getInstance().recordMemoryAllocation(size, type);
}

void CallTracer::memoryDeallocate(void *ptr) {
    if (!memory_tracing_enabled_)
        return;

    TRACE_EVENT_INSTANT(
        ("memory_free_" + std::to_string(reinterpret_cast<uintptr_t>(ptr))).c_str());

    size_t size = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = memory_allocations_.find(ptr);
        if (it != memory_allocations_.end()) {
            size = it->second;
            memory_allocations_.erase(it);
        }
    }

    Statistics::getInstance().recordMemoryDeallocation(size, "");
}

void CallTracer::memoryUsageSnapshot() {
    if (!memory_tracing_enabled_)
        return;

    size_t total_memory = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto &pair : memory_allocations_) {
            total_memory += pair.second;
        }
    }

    TRACE_EVENT_COUNTER("memory_usage", static_cast<int>(total_memory));
    Statistics::getInstance().recordMemoryUsage(total_memory, total_memory);
}

void CallTracer::fileOpen(const std::string &filename, const std::string &mode) {
    if (!io_tracing_enabled_)
        return;

    TRACE_EVENT_INSTANT(("file_open:" + filename).c_str());
    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    Statistics::getInstance().recordFileOperation("open", filename, 0, timestamp);
}

void CallTracer::fileClose(const std::string &filename) {
    if (!io_tracing_enabled_)
        return;

    TRACE_EVENT_INSTANT(("file_close:" + filename).c_str());

    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    Statistics::getInstance().recordFileOperation("close", filename, 0, timestamp);
}

void CallTracer::fileRead(const std::string &filename, size_t bytes) {
    if (!io_tracing_enabled_)
        return;

    TRACE_EVENT_COUNTER(("file_read_bytes:" + filename).c_str(), static_cast<int>(bytes));

    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    Statistics::getInstance().recordFileOperation("read", filename, bytes, timestamp);
}

void CallTracer::fileWrite(const std::string &filename, size_t bytes) {
    if (!io_tracing_enabled_)
        return;

    TRACE_EVENT_COUNTER(("file_write_bytes:" + filename).c_str(), static_cast<int>(bytes));

    auto now = std::chrono::high_resolution_clock::now();
    auto timestamp =
        std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
    Statistics::getInstance().recordFileOperation("write", filename, bytes, timestamp);
}

void CallTracer::exceptionThrown(const std::string &exception_type, const std::string &message) {
    if (!exception_tracing_enabled_)
        return;

    TRACE_EVENT_INSTANT(("exception_thrown:" + exception_type).c_str());

    Statistics::getInstance().recordException(exception_type, message);
}

void CallTracer::exceptionCaught(const std::string &exception_type) {
    if (!exception_tracing_enabled_)
        return;

    TRACE_EVENT_INSTANT(("exception_caught:" + exception_type).c_str());

    Statistics::getInstance().recordExceptionHandled(exception_type);
}

void CallTracer::setMaxDepth(int depth) { max_depth_ = depth; }

void CallTracer::setMinDuration(uint64_t microseconds) { min_duration_ = microseconds; }

void CallTracer::setOutputFile(const std::string &filename) { output_file_ = filename; }

void CallTracer::startTracing() {
    function_tracing_enabled_ = true;
    memory_tracing_enabled_ = true;
    io_tracing_enabled_ = true;
    exception_tracing_enabled_ = true;
}

void CallTracer::stopTracing() {
    function_tracing_enabled_ = false;
    memory_tracing_enabled_ = false;
    io_tracing_enabled_ = false;
    exception_tracing_enabled_ = false;
}

void CallTracer::generateReport() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream file(output_file_);
    if (!file.is_open()) {
        std::cerr << "[CallTracer] Failed to open output file: " << output_file_ << std::endl;
        return;
    }

    file << "{\n";
    file << "  \"call_tracing_report\": {\n";
    file << "    \"timestamp\": \""
         << std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count()
         << "\",\n";
    file << "    \"functions\": [\n";

    size_t index = 0;
    for (const auto &pair : function_stats_) {
        const std::string &func_name = pair.first;
        const CallStats &stats = pair.second;

        if (index > 0) {
            file << ",\n";
        }

        file << "      {\n";
        file << "        \"function_name\": \"" << func_name << "\",\n";
        file << "        \"call_count\": " << stats.call_count << ",\n";
        file << "        \"total_time_us\": " << stats.total_time << ",\n";
        file << "        \"average_time_us\": "
             << (stats.call_count > 0 ? stats.total_time / stats.call_count : 0) << ",\n";
        file << "        \"min_time_us\": "
             << (stats.min_time == std::numeric_limits<uint64_t>::max() ? 0 : stats.min_time)
             << ",\n";
        file << "        \"max_time_us\": " << stats.max_time << "\n";
        file << "      }";

        index++;
    }

    file << "\n    ]\n";
    file << "  }\n";
    file << "}\n";

    file.close();

    std::cout << "[CallTracer] Report generated: " << output_file_ << std::endl;
}

const std::unordered_map<std::string, CallTracer::CallStats> &CallTracer::getFunctionStats() const {
    return function_stats_;
}

} // namespace profiler
#else

namespace profiler {

CallTracer &CallTracer::getInstance() {
    static CallTracer instance;
    return instance;
}

void CallTracer::functionEnter(const std::string &func_name, const std::string &file, int line) {
    TRACE_EVENT_BEGIN(func_name.c_str());
}

void CallTracer::functionExit(const std::string &func_name) { TRACE_EVENT_END(); }

void CallTracer::memoryAllocate(void *ptr, size_t size, const std::string &type) {}

void CallTracer::memoryDeallocate(void *ptr) {}

void CallTracer::memoryUsageSnapshot() {}

void CallTracer::fileOpen(const std::string &filename, const std::string &mode) {}

void CallTracer::fileClose(const std::string &filename) {}

void CallTracer::fileRead(const std::string &filename, size_t bytes) {}

void CallTracer::fileWrite(const std::string &filename, size_t bytes) {}

void CallTracer::exceptionThrown(const std::string &exception_type, const std::string &message) {}

void CallTracer::exceptionCaught(const std::string &exception_type) {}

void CallTracer::enableFunctionTracing(bool enable) {}

void CallTracer::enableMemoryTracing(bool enable) {}

void CallTracer::enableIOTracing(bool enable) {}

void CallTracer::enableExceptionTracing(bool enable) {}

void CallTracer::setMaxDepth(int depth) {}

void CallTracer::setMinDuration(uint64_t microseconds) {}

void CallTracer::setOutputFile(const std::string &filename) {}

void CallTracer::startTracing() {}

void CallTracer::stopTracing() {}

void CallTracer::generateReport() {}

const std::unordered_map<std::string, CallTracer::CallStats> &CallTracer::getFunctionStats() const {
    static std::unordered_map<std::string, CallTracer::CallStats> empty_stats;
    return empty_stats;
}

} // namespace profiler

#endif
