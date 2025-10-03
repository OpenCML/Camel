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

#include "../core/trace.h"
#include <memory>
#include <string>

namespace profiler {

class VisualTracer {
  public:
    static VisualTracer &getInstance();

    void startTracing(const std::string &output_file = "profile_reports/camel_trace.json");
    void stopTracing();
    bool isTracing() const;

    void recordEventBegin(const std::string &name, const std::string &cat = "default");
    void recordEventEnd(const std::string &name = "");
    void recordEventInstant(const std::string &name, const std::string &cat = "default");
    void recordEventComplete(
        const std::string &name, uint64_t start_ts, uint64_t dur,
        const std::string &cat = "default");
    void recordCounter(const std::string &name, int value, const std::string &cat = "default");

  private:
    VisualTracer() = default;
    ~VisualTracer() = default;

    bool tracing_enabled_ = false;
    std::string output_file_;
    std::unique_ptr<class VisualTracerImpl> impl_;
};

void start_visual_tracing(const std::string &output_file = "profile_reports/camel_trace.json");
void stop_visual_tracing();
bool is_visual_tracing_active();

#define TRACE_START(output_file) profiler::start_visual_tracing(output_file)
#define TRACE_STOP() profiler::stop_visual_tracing()
#define TRACE_IS_ACTIVE() profiler::is_visual_tracing_active()

} // namespace profiler

#else

#define TRACE_START(output_file)
#define TRACE_STOP()
#define TRACE_IS_ACTIVE() false

#endif
