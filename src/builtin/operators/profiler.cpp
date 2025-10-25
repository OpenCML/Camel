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
 * Created: Oct. 1, 2025
 * Updated: Oct. 25, 2025
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
    const data_ptr_t &arg = frame.get(nargs[0]);
    if (arg == nullptr) {
        return;
    }

    ASSERT(arg->type()->code() == TypeCode::String, "profiler.begin requires a string argument");

    auto stringData = std::dynamic_pointer_cast<StringData>(arg);
    if (stringData == nullptr) {
        return;
    }

#ifndef NDEBUG
    const std::string &name = stringData->data();
    bool is_tracing = profiler::AdvancedTracer::getInstance().isTracing();
    if (is_tracing) {
        profiler::AdvancedTracer::getInstance().traceFunctionCall(name);
    } else {
    }
    std::cout << "[PROFILER] Begin: " << name << std::endl;
#endif

    return;
}
void __profiler_end__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arg = frame.get(nargs[0]);
    ASSERT(arg->type()->code() == TypeCode::String, "profiler.end requires a string argument");

    auto stringData = std::dynamic_pointer_cast<StringData>(arg);
    if (stringData == nullptr) {
        return;
    }

#ifndef NDEBUG
    const std::string &name = stringData->data();
    if (profiler::AdvancedTracer::getInstance().isTracing()) {
        profiler::AdvancedTracer::getInstance().traceFunctionReturn(name);
    }
    std::cout << "[PROFILER] End: " << name << std::endl;
#endif

    return;
}

void __profiler_instant__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arg = frame.get(nargs[0]);
    ASSERT(arg->type()->code() == TypeCode::String, "profiler.instant requires a string argument");

    auto stringData = std::dynamic_pointer_cast<StringData>(arg);
    if (stringData == nullptr) {
        return;
    }

#ifndef NDEBUG
    const std::string &name = stringData->data();
    if (profiler::AdvancedTracer::getInstance().isTracing()) {
        profiler::TRACE_EVENT_INSTANT(name.c_str());
    }
    std::cout << "[PROFILER] Instant: " << name << std::endl;
#endif

    return;
}

void __profiler_enable__(
    GraphIR::data_idx_t self, data_arr_t nargs, data_arr_t wargs, Frame &frame, Context &ctx) {
    const data_ptr_t &arg = frame.get(nargs[0]);
    ASSERT(arg->type()->code() == TypeCode::Bool, "profiler.enable requires a boolean argument");

    auto boolData = std::dynamic_pointer_cast<PrimaryData<bool>>(arg);

#ifndef NDEBUG
    const bool &enabled = boolData->data();
    if (enabled) {
        profiler::AdvancedTracer::Config config;
        config.enablePerfettoIntegration = true;
        config.perfettoOutput = "profile_reports/camel_trace.perfetto-trace";
        config.outputFile = "profile_reports/camel_trace.json";
        profiler::AdvancedTracer::getInstance().initialize(config);
        profiler::AdvancedTracer::getInstance().startTracing();
        std::cout << "[PROFILER] Profiling enabled" << std::endl;
    } else {
        profiler::AdvancedTracer::getInstance().stopTracing();
        profiler::trace_event_flush();
        std::cout << "[PROFILER] Profiling disabled. Trace data saved to "
                     "profile_reports/trace_output.json"
                  << std::endl;
    }
#endif

    return;
}
