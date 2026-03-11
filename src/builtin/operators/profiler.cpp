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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "profiler.h"
#include "camel/compile/gir.h"
#include "camel/core/context/context.h"
#include "camel/core/data.h"
#include "camel/core/data/primary.h"
#include "camel/core/module/builtin.h"
#include "camel/core/operator.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"
#include "service/profiler/core/trace.h"

#ifndef NDEBUG
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#endif

#include <iostream>
#include <string>

using namespace camel::core::context;

slot_t __profiler_begin__(ArgsView &with, ArgsView &norm, Context &ctx) {
    EXEC_WHEN_DEBUG({
        const std::string name = "profiler_scope";
        bool is_tracing        = profiler::AdvancedTracer::getInstance().isTracing();
        if (is_tracing) {
            profiler::AdvancedTracer::getInstance().traceFunctionCall(name);
        }
        std::cout << "[PROFILER] Begin: " << name << std::endl;
    });
    return NullSlot;
}

slot_t __profiler_end__(ArgsView &with, ArgsView &norm, Context &ctx) {
    EXEC_WHEN_DEBUG({
        const std::string name = "profiler_scope";
        if (profiler::AdvancedTracer::getInstance().isTracing()) {
            profiler::AdvancedTracer::getInstance().traceFunctionReturn(name);
        }
        std::cout << "[PROFILER] End: " << name << std::endl;
    });
    return NullSlot;
}

slot_t __profiler_instant__(ArgsView &with, ArgsView &norm, Context &ctx) {
    EXEC_WHEN_DEBUG({
        const std::string name = "instant_event";
        if (profiler::AdvancedTracer::getInstance().isTracing()) {
            profiler::TRACE_EVENT_INSTANT(name.c_str());
        }
        std::cout << "[PROFILER] Instant: " << name << std::endl;
    });
    return NullSlot;
}

slot_t __profiler_enable__(ArgsView &with, ArgsView &norm, Context &ctx) {
    EXEC_WHEN_DEBUG({
        profiler::AdvancedTracer::Config config;
        config.enablePerfettoIntegration = true;
        config.perfettoOutput            = "profile_reports/camel_trace.perfetto-trace";
        config.outputFile                = "profile_reports/camel_trace.json";
        profiler::AdvancedTracer::getInstance().initialize(config);
        profiler::AdvancedTracer::getInstance().startTracing();
        std::cout << "[PROFILER] Profiling enabled" << std::endl;
    });
    return NullSlot;
}
