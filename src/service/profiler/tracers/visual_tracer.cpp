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

#include "visual_tracer.h"
#include "../analysis/perfetto_integration.h"
#include <iostream>

#ifndef NDEBUG

namespace profiler {

class VisualTracerImpl {
  public:
    void start(const std::string &output_file) {
        std::cout << "[PROFILER TRACE] Starting visual tracing to: " << output_file << std::endl;
        // Integrate Perfetto tracing
        PERFETTO_START(output_file);
    }

    void stop() {
        std::cout << "[PROFILER TRACE] Stopping visual tracing" << std::endl;
        // Stop tracing and open Perfetto
        PERFETTO_STOP_AND_OPEN();
    }

    void recordEventBegin(const std::string &name, const std::string &cat) {
        trace_event_begin(name.c_str(), cat.c_str());
    }

    void recordEventEnd(const std::string &name) { trace_event_end(); }

    void recordEventInstant(const std::string &name, const std::string &cat) {
        trace_event_instant(name.c_str(), cat.c_str());
    }

    void recordEventComplete(
        const std::string &name, uint64_t start_ts, uint64_t dur, const std::string &cat) {
        // Use existing profiler API
        trace_event_complete(name.c_str(), start_ts, dur, cat.c_str());
    }

    void recordCounter(const std::string &name, int value, const std::string &cat) {
        trace_event_counter(name.c_str(), value, cat.c_str());
    }
};

VisualTracer &VisualTracer::getInstance() {
    static VisualTracer instance;
    return instance;
}

void VisualTracer::startTracing(const std::string &output_file) {
    if (!tracing_enabled_) {
        impl_ = std::make_unique<VisualTracerImpl>();
        impl_->start(output_file);
        output_file_ = output_file;
        tracing_enabled_ = true;
    }
}

void VisualTracer::stopTracing() {
    if (tracing_enabled_ && impl_) {
        impl_->stop();
        impl_.reset();
        tracing_enabled_ = false;
    }
}

bool VisualTracer::isTracing() const { return tracing_enabled_; }

void VisualTracer::recordEventBegin(const std::string &name, const std::string &cat) {
    if (tracing_enabled_ && impl_) {
        impl_->recordEventBegin(name, cat);
    }
}

void VisualTracer::recordEventEnd(const std::string &name) {
    if (tracing_enabled_ && impl_) {
        impl_->recordEventEnd(name);
    }
}

void VisualTracer::recordEventInstant(const std::string &name, const std::string &cat) {
    if (tracing_enabled_ && impl_) {
        impl_->recordEventInstant(name, cat);
    }
}

void VisualTracer::recordEventComplete(
    const std::string &name, uint64_t start_ts, uint64_t dur, const std::string &cat) {
    if (tracing_enabled_ && impl_) {
        impl_->recordEventComplete(name, start_ts, dur, cat);
    }
}

void VisualTracer::recordCounter(const std::string &name, int value, const std::string &cat) {
    if (tracing_enabled_ && impl_) {
        impl_->recordCounter(name, value, cat);
    }
}

void start_visual_tracing(const std::string &output_file) {
    VisualTracer::getInstance().startTracing(output_file);
}

void stop_visual_tracing() { VisualTracer::getInstance().stopTracing(); }

bool is_visual_tracing_active() { return VisualTracer::getInstance().isTracing(); }

} // namespace profiler

#endif
