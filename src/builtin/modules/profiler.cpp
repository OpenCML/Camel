// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "profiler.h"

using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::type;

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
