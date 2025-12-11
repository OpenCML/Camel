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
 * Created: Oct. 01, 2025
 * Updated: Dec. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "profiler.h"
#include "compile/gir.h"
#include "core/context/context.h"
#include "core/context/frame.h"
#include "core/data/data.h"
#include "core/data/primary.h"
#include "core/module/builtin.h"
#include "service/profiler/core/trace.h"
#include "utils/assert.h"
#include "utils/log.h"

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

#include <iostream>
#include <string>

void __profiler_begin__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
#ifndef NDEBUG
    const std::string name = "profiler_scope";
    bool is_tracing        = profiler::AdvancedTracer::getInstance().isTracing();
    if (is_tracing) {
        profiler::AdvancedTracer::getInstance().traceFunctionCall(name);
    } else {
    }
    std::cout << "[PROFILER] Begin: " << name << std::endl;
#endif
}

void __profiler_end__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
#ifndef NDEBUG
    const std::string name = "profiler_scope";
    if (profiler::AdvancedTracer::getInstance().isTracing()) {
        profiler::AdvancedTracer::getInstance().traceFunctionReturn(name);
    }
    std::cout << "[PROFILER] End: " << name << std::endl;
#endif
}

void __profiler_instant__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
#ifndef NDEBUG
    const std::string name = "instant_event";
    if (profiler::AdvancedTracer::getInstance().isTracing()) {
        profiler::TRACE_EVENT_INSTANT(name.c_str());
    }
    std::cout << "[PROFILER] Instant: " << name << std::endl;
#endif
}

void __profiler_enable__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
#ifndef NDEBUG
    profiler::AdvancedTracer::Config config;
    config.enablePerfettoIntegration = true;
    config.perfettoOutput            = "profile_reports/camel_trace.perfetto-trace";
    config.outputFile                = "profile_reports/camel_trace.json";
    profiler::AdvancedTracer::getInstance().initialize(config);
    profiler::AdvancedTracer::getInstance().startTracing();
    std::cout << "[PROFILER] Profiling enabled" << std::endl;
#endif
}
