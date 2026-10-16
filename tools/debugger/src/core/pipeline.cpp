/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Oct. 15, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Release-capable compiler boundary stops. Capture data on the program thread before
// waiting; inspector requests never keep GC pointers or run a second compilation.
#include "camel/passes/trans/json/gir.h"
#include "debug_core.h"

namespace debugger::core {
void DebugCore::pipelineBoundary(
    size_t index, const std::string &pass, camel::runtime::GCGraph *graph) {
    bool requested;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        requested = (index == 0 && stopOnPassEntry_) || passBreakpoints_.contains(index);
    }
    const bool interrupted = pauseRequested_.exchange(false);
    if (!requested && step_.kind != StepKind::Pass && !interrupted)
        return;
    pipelineGir_ = dumpJsonRuntimeGraph(graph, index_.context());
    if (pipelineGir_.empty())
        return;
    struct Clear {
        std::string &text;
        ~Clear() { text.clear(); }
    } clear{pipelineGir_};
    stop(
        "pause",
        json{
            {"pipeline",
             {{"index", index},
              {"pass", pass.empty() ? json(nullptr) : json(pass)},
              {"boundary", index == 0 ? "initial" : "after"}}}});
}
} // namespace debugger::core
