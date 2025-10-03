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

#pragma once

#include "core/module/builtin.h"
#include <string>

class ProfilerBuiltinModule : public BuiltinModule {
  private:
    bool enabled_ = false;
    bool tracing_ = false;
    bool realtime_analysis_ = false;
    std::string output_file_ = "profiler_reports/camel_trace.json";
    std::string perfetto_output_file_ = "profiler_reports/camel_trace.perfetto-trace";
    std::string call_graph_output_file_ = "profiler_reports/call_graph.dot";
    std::string detailed_report_output_file_ = "profiler_reports/detailed_report.md";

  public:
    ProfilerBuiltinModule(context_ptr_t ctx);
    virtual ~ProfilerBuiltinModule() = default;

    virtual bool load() override;

    void begin(const std::string &name);
    void end(const std::string &name);
    void instant(const std::string &name);
    void enable(bool enabled);

    static module_ptr_t create(context_ptr_t ctx) {
        return std::make_shared<ProfilerBuiltinModule>(ctx);
    }
};
