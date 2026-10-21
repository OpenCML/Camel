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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel_pass.h"

#include "camel/core/error/diagnostics.h"
#include "camel/utils/assert.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/pass/base.h"
#include "camel/runtime/definitions.h"
#include "passes/sched/nodevm/nodevm.h"

#include <format>

using camel::core::context::context_ptr_t;
using camel::core::error::RuntimeDiag;
using camel::runtime::Program;

namespace camel::execute {

namespace {

/// Whether `graph` has the signature of a pass: one ordinary parameter of type Program, no
/// with parameter, result `Program!`.
bool hasPassSignature(const camel::runtime::GCGraph &graph) {
    const auto *type = graph.funcType();
    if (type == nullptr || type->normTypesCount() != 1 || type->withTypesCount() != 0 ||
        type->normTypeAt(0)->toString() != "Program") {
        return false;
    }
    const auto *result = camel::core::type::asFallible(type->exitType());
    return result != nullptr && result->payloadType()->toString() == "Program";
}

class CamelPass final : public GraphIRPass {
  public:
    CamelPass(const context_ptr_t &ctx, std::string module, std::string function)
        : GraphIRPass(ctx), module_(std::move(module)), function_(std::move(function)) {}

    const Program *apply(const Program *program, std::ostream &os) override {
        (void)os;
        std::string problem;
        bool absent     = false;
        const auto tool = context_->definitions().libraryFunction(module_, function_, problem, absent);
        ASSERT(tool.has_value(), "A registered Camel pass is available.");
        NodeVMSchedPass vm(context_);
        const slot_t argument = camel::core::rtdata::toSlot(const_cast<Program *>(program));
        const slot_t result   = vm.callGraph(tool->program, tool->graph, std::span(&argument, 1));
        if (camel::core::rtdata::isErrorValue(result)) {
            std::ostringstream text;
            camel::core::rtdata::printTaggedSlots(
                text,
                camel::core::rtdata::fromSlot<::Tuple *>(result)->data(),
                camel::core::type::asFallible(tool->graph->funcType()->exitType()));
            context_->rtmDiags()->of(RuntimeDiag::GraphPassFailed).commit(module_ + "::" + function_, text.str());
            return nullptr;
        }
        const auto *produced = camel::core::rtdata::fromSlot<const Program *>(
            camel::core::rtdata::falliblePayload(result));
        if (produced != program) {
            context_->installRuntimeProgram(produced);
        }
        return produced;
    }

  private:
    std::string module_;
    std::string function_;
};

} // namespace

void registerCamelPass(
    const context_ptr_t &ctx, const std::string &module, const std::string &function,
    std::string &problem) {
    bool absent     = false;
    const auto tool = ctx->definitions().libraryFunction(module, function, problem, absent);
    if (absent) {
        problem.clear();
        return;
    }
    if (!tool) {
        return;
    }
    if (!hasPassSignature(*tool->graph)) {
        problem = std::format(
            "'{}::{}' is not a pass: a pass is a function (p: Program) => Program!",
            module,
            function);
        return;
    }
    registerModulePass(module + "::" + function, [module, function](const context_ptr_t &ctx) {
        return std::make_unique<CamelPass>(ctx, module, function);
    });
}

} // namespace camel::execute
