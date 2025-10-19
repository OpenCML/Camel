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
 * Updated: Oct. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "profiler.h"
#ifndef NDEBUG
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

static const std::vector<oper_group_ptr_t> &getOperatorGroups() {
    static const std::vector<oper_group_ptr_t> groups = {
        OperatorGroup::create(
            "begin",
            {
                {
                    ":profiler/begin",
                    StaticFuncTypeResolver::create({}, {}, Type::Void()),
                },
            }),
        OperatorGroup::create(
            "end",
            {
                {
                    ":profiler/end",
                    StaticFuncTypeResolver::create({}, {}, Type::Void()),
                },
            }),
        OperatorGroup::create(
            "instant",
            {
                {
                    ":profiler/instant",
                    StaticFuncTypeResolver::create({}, {}, Type::Void()),
                },
            }),
        OperatorGroup::create(
            "enable",
            {
                {
                    ":profiler/enable",
                    StaticFuncTypeResolver::create({}, {}, Type::Void()),
                },
            }),
    };

    return groups;
}

ProfilerBuiltinModule::ProfilerBuiltinModule(context_ptr_t ctx) : BuiltinModule("profiler", ctx) {
    for (const auto &group : getOperatorGroups()) {
        exportEntity(group->name(), group);
    }
}

bool ProfilerBuiltinModule::load() {
    if (loaded_) {
        return true;
    }
    loaded_ = true;
    return true;
}

module_ptr_t ProfilerBuiltinModule::create(context_ptr_t ctx) {
    return std::make_shared<ProfilerBuiltinModule>(ctx);
}
#endif
