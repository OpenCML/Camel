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
 * Updated: May. 05, 2026
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
#include "camel/core/type/base.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"

#include <functional>
#include <iosfwd>
#include <memory>
#include <optional>
#include <span>
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
    explicit MacroEvaluator(const camel::core::context::context_ptr_t &context);
    ~MacroEvaluator();

    std::optional<MacroEvalResult> tryEvaluate(const MacroCandidate &candidate, std::ostream &os);

  private:
    camel::core::context::context_ptr_t context_;
    camel::core::context::FramePool framePool_;
    std::unique_ptr<MacroStaticAreaStore> staticAreas_;
    size_t recursionDepth_ = 0;

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
    slot_t executeGraph(camel::core::context::Frame *frame, camel::runtime::GCGraph *runtimeGraph);
};

std::vector<MacroCandidate>
collectMacroCandidates(std::span<camel::runtime::GCGraph *const> closure);

bool applyMacroRewrite(
    camel::runtime::RuntimeGraphDraftSession &session, const MacroCandidate &candidate,
    const MacroEvalResult &result, std::ostream &os);

} // namespace camel::execute::macro_runtime
