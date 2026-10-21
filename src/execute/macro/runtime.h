/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 04, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Macro-specific rewrite model.
 *
 * The runtime-native macro pipeline is split into three layers:
 * - candidate collection over the reachable runtime closure
 * - compile-time evaluation against a read-only source graph
 * - draft rewriting that materializes the evaluated result into the next graph
 */

#pragma once

#include "camel/core/context/frame.h"
#include "camel/core/rtdata/base.h"
#include "camel/core/source/ids.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"

#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace camel::execute::macro_runtime {

class MacroStaticAreaStore;

struct MacroCandidate {
    camel::runtime::GCGraph *runtimeGraph = nullptr;
    camel::runtime::gc_node_ref_t nodeRef = camel::runtime::kInvalidNodeRef;
};

struct MacroEvalResult {
    const slot_t *valueSlot       = nullptr;
    camel::core::type::Type *type = nullptr;
    uint8_t runtimeFlags          = 0;

    slot_t value() const { return valueSlot ? *valueSlot : NullSlot; }
    bool valid() const { return valueSlot != nullptr && type != nullptr; }
};

class MacroEvaluator {
  public:
    /// An evaluator of the macro calls of the graphs of `program`.
    MacroEvaluator(
        const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program);
    ~MacroEvaluator();

    std::optional<MacroEvalResult> tryEvaluate(const MacroCandidate &candidate, std::ostream &os);

    /// An evaluator belongs to one rewrite round. Only failures from the final round,
    /// in which nothing changes any more, are reported: another expansion can make a
    /// previously unsuccessful call evaluable.
    /// Reports the failures of the current round as RuntimeDiag::MacroEvaluationFailed errors
    /// at the call, with the reason, in the runtime diagnostics: a failed evaluation rejects
    /// the program (design principles 8). An argument that is not a
    /// constant is not a failure and is never reported here.
    void reportFailures();

  private:
    struct Failure {
        uint64_t origin = 0;
        std::string callee;
        std::string reason;
    };
    std::vector<Failure> failures_;
    /// `origin`, when valid, is the place the failure is about (a fault that names one): the
    /// error is reported there, and its text says where the macro call is.
    void noteFailure(
        const MacroCandidate &candidate, std::string reason,
        camel::source::origin_id_t origin = camel::source::kInvalidOriginId);
    /// The normal reason a macro call is not evaluated: an argument that is not a constant.
    /// Logged at warn level (visible with -v), naming the argument.
    void noteNotStatic(
        camel::runtime::GCGraph *ownerGraph, camel::runtime::gc_node_ref_t nodeRef,
        std::string_view callee, std::span<const camel::runtime::gc_node_ref_t> withInputs,
        std::span<const camel::runtime::gc_node_ref_t> normInputs, size_t withStart = 0) const;

    camel::core::context::context_ptr_t context_;
    const camel::runtime::Program *program_;
    camel::core::context::FramePool framePool_;
    std::unique_ptr<MacroStaticAreaStore> staticAreas_;
    size_t recursionDepth_ = 0;
    // Nested calls one evaluation may open (camel/execute/limits.h).
    size_t maxDepth_;

    struct ValueRoot;
    std::vector<std::unique_ptr<ValueRoot>> valueRoots_;

    MacroEvalResult anchorResult(slot_t value, camel::core::type::Type *type, uint8_t flags);
    void
    traceValueRoots(const camel::core::mm::GenerationalAllocatorWithGC::RefRelocator &relocate);
    bool macroCallsFunctionParam(camel::runtime::GCGraph *graph) const;
    std::optional<MacroEvalResult> tryExecuteDirectFunc(
        camel::runtime::GCGraph *ownerGraph, camel::runtime::gc_node_ref_t nodeRef,
        std::ostream &os);
    std::optional<MacroEvalResult> tryExecuteIndirectCall(
        camel::runtime::GCGraph *ownerGraph, camel::runtime::gc_node_ref_t nodeRef,
        std::ostream &os);
    std::optional<MacroEvalResult> tryExecuteStaticOper(
        camel::runtime::GCGraph *ownerGraph, camel::runtime::gc_node_ref_t nodeRef,
        std::ostream &os);
    slot_t executeFunction(
        ::Function *funcObj,
        const std::function<void(camel::core::context::Frame *, camel::runtime::GCGraph *)>
            &fillArgs,
        bool requireMacroGraph);
    /// Runs the native operator `op` (the operator `oper`) with its arguments. The call has a
    /// function-call scope of its own (camel/execute/function_call.h): an operator that calls
    /// graphs calls them in this evaluator, with its depth and its stack check; any other masks
    /// the scope of whatever runs outside (design: native subgraph call, item 7).
    slot_t callNativeOperator(
        operator_t op, camel::core::OperId oper, ArgsView &withView, ArgsView &normView);
    slot_t executeGraph(camel::core::context::Frame *frame, camel::runtime::GCGraph *runtimeGraph);
    /// The LOOP node `node` of `graph` run in `frame` (camel/runtime/loop.h), round by round:
    /// the loop is not a recursion, so it takes no depth, only kMaxMacroLoopRounds rounds.
    slot_t executeLoop(
        camel::core::context::Frame *frame, camel::runtime::GCGraph *graph,
        camel::runtime::gc_node_ref_t node);
};

std::vector<MacroCandidate>
collectMacroCandidates(std::span<camel::runtime::GCGraph *const> closure);

bool applyMacroRewrite(
    camel::runtime::RuntimeGraphDraftSession &session, const MacroCandidate &candidate,
    const MacroEvalResult &result, std::ostream &os);

} // namespace camel::execute::macro_runtime
