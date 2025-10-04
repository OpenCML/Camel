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
 * Updated: Oct. 04, 2025
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
OperatorReturnCode __profiler_begin__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    // std::cout << "[PROFILER_DEBUG] Entering __profiler_begin__" << std::endl;
    const auto &ins = self->normInputs();
    // std::cout << "[PROFILER_DEBUG] Got inputs, size: " << ins.size() << std::endl;
    ASSERT(ins.size() == 1, "profiler.begin requires exactly one argument");

    const data_ptr_t &arg = frame.get(ins[0]);
    // std::cout << "[PROFILER_DEBUG] Got argument from frame, arg is null: " << (arg == nullptr ?
    // "true" : "false") << std::endl;
    if (arg == nullptr) {
        // std::cout << "[PROFILER_DEBUG] Argument is null, cannot proceed" << std::endl;
        return OperatorReturnCode::Error;
    }

    // std::cout << "[PROFILER_DEBUG] Argument type code: " << static_cast<int>(arg->type()->code())
    // << std::endl;
    ASSERT(arg->type()->code() == TypeCode::String, "profiler.begin requires a string argument");

    // std::cout << "[PROFILER_DEBUG] Casting argument to StringData" << std::endl;
    auto stringData = std::dynamic_pointer_cast<StringData>(arg);
    // std::cout << "[PROFILER_DEBUG] Checking if stringData is null: " << (stringData == nullptr ?
    // "true" : "false") << std::endl;
    if (stringData == nullptr) {
        // std::cout << "[PROFILER_DEBUG] stringData is null, cannot proceed" << std::endl;
        return OperatorReturnCode::Error;
    }

    // std::cout << "[PROFILER_DEBUG] Getting string value" << std::endl;
    const std::string &name = stringData->data();
    // std::cout << "[PROFILER_DEBUG] Profiler begin called with name: " << name << std::endl;

#ifndef NDEBUG
    // std::cout << "[PROFILER_DEBUG] Checking if AdvancedTracer is tracing" << std::endl;
    bool is_tracing = profiler::AdvancedTracer::getInstance().isTracing();
    // std::cout << "[PROFILER_DEBUG] AdvancedTracer is tracing: " << (is_tracing ? "true" :
    // "false") << std::endl;
    if (is_tracing) {
        // std::cout << "[PROFILER_DEBUG] AdvancedTracer is tracing, calling traceFunctionCall" <<
        // std::endl;
        profiler::AdvancedTracer::getInstance().traceFunctionCall(name);
        // std::cout << "[PROFILER_DEBUG] traceFunctionCall completed" << std::endl;
    } else {
        // std::cout << "[PROFILER_DEBUG] AdvancedTracer is not tracing, skipping traceFunctionCall"
        // << std::endl;
    }
    std::cout << "[PROFILER] Begin: " << name << std::endl;
    // std::cout << "[PROFILER_DEBUG] Exiting __profiler_begin__" << std::endl;
#endif

    // std::cout << "[PROFILER_DEBUG] __profiler_begin__ returning OK" << std::endl;
    return OperatorReturnCode::OK;
}
OperatorReturnCode __profiler_end__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 1, "profiler.end requires exactly one argument");

    const data_ptr_t &arg = frame.get(ins[0]);
    ASSERT(arg->type()->code() == TypeCode::String, "profiler.end requires a string argument");

    auto stringData = std::dynamic_pointer_cast<StringData>(arg);
    if (stringData == nullptr) {
        return OperatorReturnCode::Error;
    }
    const std::string &name = stringData->data();

#ifndef NDEBUG
    if (profiler::AdvancedTracer::getInstance().isTracing()) {
        profiler::AdvancedTracer::getInstance().traceFunctionReturn(name);
    }
    std::cout << "[PROFILER] End: " << name << std::endl;
#endif

    return OperatorReturnCode::OK;
}

OperatorReturnCode __profiler_instant__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 1, "profiler.instant requires exactly one argument");

    const data_ptr_t &arg = frame.get(ins[0]);
    ASSERT(arg->type()->code() == TypeCode::String, "profiler.instant requires a string argument");

    auto stringData = std::dynamic_pointer_cast<StringData>(arg);
    if (stringData == nullptr) {
        return OperatorReturnCode::Error;
    }
    const std::string &name = stringData->data();

#ifndef NDEBUG
    if (profiler::AdvancedTracer::getInstance().isTracing()) {
        profiler::TRACE_EVENT_INSTANT(name.c_str());
    }
    std::cout << "[PROFILER] Instant: " << name << std::endl;
#endif

    return OperatorReturnCode::OK;
}

OperatorReturnCode __profiler_enable__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx) {
    const auto &ins = self->normInputs();
    ASSERT(ins.size() == 1, "profiler.enable requires exactly one argument");

    const data_ptr_t &arg = frame.get(ins[0]);
    ASSERT(arg->type()->code() == TypeCode::Bool, "profiler.enable requires a boolean argument");

    auto boolData = std::dynamic_pointer_cast<PrimaryData<bool>>(arg);
    const bool &enabled = boolData->data();

#ifndef NDEBUG
    // std::cout << "[PROFILER_DEBUG] Profiler enable called with value: " << (enabled ? "true" :
    // "false") << std::endl;
    if (enabled) {
        // std::cout << "[PROFILER_DEBUG] Enabling profiling..." << std::endl;
        profiler::AdvancedTracer::Config config;
        config.enablePerfettoIntegration = true;
        config.perfettoOutput = "profile_reports/camel_trace.perfetto-trace";
        config.outputFile = "profile_reports/camel_trace.json";
        profiler::AdvancedTracer::getInstance().initialize(config);
        profiler::AdvancedTracer::getInstance().startTracing();
        std::cout << "[PROFILER] Profiling enabled" << std::endl;
        // std::cout << "[PROFILER_DEBUG] Profiling enabled successfully" << std::endl;
    } else {
        // std::cout << "[PROFILER_DEBUG] Disabling profiling..." << std::endl;
        profiler::AdvancedTracer::getInstance().stopTracing();
        // std::cout << "[PROFILER_DEBUG] Tracing stopped, flushing events..." << std::endl;
        // Ensure trace data is written to file
        profiler::trace_event_flush();
        std::cout << "[PROFILER] Profiling disabled. Trace data saved to "
                     "profile_reports/trace_output.json"
                  << std::endl;
        // std::cout << "[PROFILER_DEBUG] Profiling disabled successfully" << std::endl;
    }
    // std::cout << "[PROFILER_DEBUG] Profiler enable function completed" << std::endl;
#endif

    return OperatorReturnCode::OK;
}
