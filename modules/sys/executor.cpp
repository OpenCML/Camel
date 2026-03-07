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
 * Created: Jul. 29, 2025
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "executor.h"
#include "camel/compile/gir.h"
#include "camel/core/context/frame.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"
#include "camel/execute/executor.h"
#include "operators.h"

#include <string>

using namespace camel::core::error;
using namespace camel::core::context;
#include <vector>

namespace {
class SysExecutor : public Executor {
  public:
    SysExecutor(context_ptr_t ctx, std::unordered_map<std::string, operator_t> ops)
        : Executor(ctx, std::move(ops)) {}
    void eval(std::string uri, GIR::Node *self, Frame &frame) override {
        auto it = opsMap_.find(uri);
        if (it == opsMap_.end())
            throw DiagnosticBuilder::of(RuntimeDiag::UnrecognizedOperatorURI).commit(uri);
        std::vector<GIR::data_idx_t> normIndices, withIndices;
        for (const auto &in : self->normInputs())
            normIndices.push_back(in->index());
        for (const auto &in : self->withInputs())
            withIndices.push_back(in->index());
        data_arr_t nargs{normIndices.data(), static_cast<GIR::arr_size_t>(normIndices.size())};
        data_arr_t wargs{withIndices.data(), static_cast<GIR::arr_size_t>(withIndices.size())};
        FrameArgsView withView(frame, wargs);
        FrameArgsView normView(frame, nargs);
        slot_t result = it->second(withView, normView, *context_);
        frame.set(self->index(), result);
    }
};
} // namespace

executor_ptr_t createSysExecutor(context_ptr_t ctx) {
    return std::make_shared<SysExecutor>(ctx, getSysOpsMap());
}
