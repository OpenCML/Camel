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
 * Created: Sep. 28, 2025
 * Updated: Oct. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "profiler.h"
#include "../../service/profiler/advanced/advanced_tracer.h"
#include "../../service/profiler/analysis/statistics.h"
#include "../../service/profiler/core/trace.h"
#include "../../service/profiler/tracers/call_tracer.h"
#include <fstream>
#include <iostream>

using namespace std;

void ProfilerBuiltinModule::begin(const string &name) {
    if (!enabled_)
        return;
    profiler::trace_event_begin(name.c_str());
}

void ProfilerBuiltinModule::end(const string &name) {
    if (!enabled_)
        return;
    profiler::trace_event_end();
}

void ProfilerBuiltinModule::instant(const string &name) {
    if (!enabled_)
        return;
    profiler::trace_event_instant(name.c_str());
}

void ProfilerBuiltinModule::enable(bool enabled) {
    enabled_ = enabled;

    if (enabled) {
        profiler::AdvancedTracer::getInstance().initializeWithDefaults();
        profiler::AdvancedTracer::getInstance().startTracing();
    } else {
        profiler::AdvancedTracer::getInstance().stopTracing();
        profiler::trace_event_flush();
    }
}

ProfilerBuiltinModule::ProfilerBuiltinModule(context_ptr_t ctx) : BuiltinModule("profiler", ctx) {}

bool ProfilerBuiltinModule::load() {
    if (loaded_) {
        return true;
    }

    exportBuiltinOperator(
        "begin",
        param_init_list{},
        {{"name", Type::String(), nullptr, false}},
        Type::Void(),
        ":profiler/begin");

    exportBuiltinOperator(
        "end",
        param_init_list{},
        {{"name", Type::String(), nullptr, false}},
        Type::Void(),
        ":profiler/end");

    exportBuiltinOperator(
        "instant",
        param_init_list{},
        {{"name", Type::String(), nullptr, false}},
        Type::Void(),
        ":profiler/instant");

    exportBuiltinOperator(
        "enable",
        param_init_list{},
        {{"enabled", Type::Bool(), nullptr, false}},
        Type::Void(),
        ":profiler/enable");

    loaded_ = true;
    return true;
}
