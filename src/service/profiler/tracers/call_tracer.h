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

#pragma once

#include "service/profiler/core/trace.h"
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace profiler {

class CallTracer {
  public:
    static CallTracer &getInstance();

    void enableFunctionTracing(bool enable = true);
    void enableMemoryTracing(bool enable = true);
    void enableIOTracing(bool enable = true);
    void enableExceptionTracing(bool enable = true);

    void functionEnter(const std::string &func_name, const std::string &file = "", int line = 0);
    void functionExit(const std::string &func_name);

    void memoryAllocate(void *ptr, size_t size, const std::string &type = "");
    void memoryDeallocate(void *ptr);
    void memoryUsageSnapshot();

    void fileOpen(const std::string &filename, const std::string &mode);
    void fileClose(const std::string &filename);
    void fileRead(const std::string &filename, size_t bytes);
    void fileWrite(const std::string &filename, size_t bytes);

    void exceptionThrown(const std::string &exception_type, const std::string &message);
    void exceptionCaught(const std::string &exception_type);

    void setMaxDepth(int depth);
    void setMinDuration(uint64_t microseconds);
    void setOutputFile(const std::string &filename);

    void startTracing();
    void stopTracing();
    void generateReport();

    struct CallStats {
        std::string function_name;
        uint64_t call_count = 0;
        uint64_t total_time = 0;
        uint64_t min_time = std::numeric_limits<uint64_t>::max();
        uint64_t max_time = 0;
        std::vector<std::string> callers;
        std::vector<std::string> callees;
    };

    const std::unordered_map<std::string, CallStats> &getFunctionStats() const;

  private:
    CallTracer() = default;
    ~CallTracer() = default;

    struct CallFrame {
        std::string function_name;
        std::string file;
        int line;
        uint64_t start_time;
        int depth;
    };

#ifndef NDEBUG
    bool function_tracing_enabled_ = false;
    bool memory_tracing_enabled_ = false;
    bool io_tracing_enabled_ = false;
    bool exception_tracing_enabled_ = false;
    int max_depth_ = 100;
    uint64_t min_duration_ = 0;
#endif
    std::string output_file_ = "profiler_reports/call_trace.json";

    std::vector<CallFrame> call_stack_;
    std::unordered_map<std::string, CallStats> function_stats_;
    std::unordered_map<void *, size_t> memory_allocations_;

    mutable std::mutex mutex_;
};

#ifndef TRACE_FUNCTION
#define TRACE_FUNCTION()                                                                           \
    profiler::CallTracer::getInstance().functionEnter(__FUNCTION__, __FILE__, __LINE__);           \
    auto __profiler_trace_guard = profiler::ScopeGuard(                                            \
        []() { profiler::CallTracer::getInstance().functionExit(__FUNCTION__); })
#endif

#ifndef TRACE_FUNCTION_NAME
#define TRACE_FUNCTION_NAME(name)                                                                  \
    profiler::CallTracer::getInstance().functionEnter(name, __FILE__, __LINE__);                   \
    auto __profiler_trace_guard_##name =                                                           \
        profiler::ScopeGuard([&]() { profiler::CallTracer::getInstance().functionExit(name); })

} // namespace profiler

#else

#define TRACE_FUNCTION()
#define TRACE_FUNCTION_NAME(name)

#endif // NDEBUG
