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
 * Created: Mar. 11, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "cpp_export.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"
#include "camel/execute/entry_args.h"

namespace {

using camel::passes::cpp::CppEmitMode;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;

/// Whether the entry function takes parameters: the entry graph has ports of its own, or
/// (their values were bound) it calls the entry function with arguments. A `main` that only
/// captures is called through a closure (FILL), which the emitter handles.
bool entryTakesParameters(const GCGraph *root) {
    if (!root->withPorts().empty() || !root->normPorts().empty()) {
        return true;
    }
    const auto callRef = root->returnNodeRef();
    const auto *call   = root->node(callRef);
    if (!call || call->kind != GCNodeKind::Call) {
        return false;
    }
    const auto with = root->withInputsOf(callRef);
    if (with.empty()) {
        return false;
    }
    const auto *callee = root->node(with.front());
    return callee && callee->kind != GCNodeKind::Fill &&
           (with.size() > 1 || !root->normInputsOf(callRef).empty());
}

const char *passName(CppEmitMode mode) {
    switch (mode) {
    case CppEmitMode::Module:
        return "std::cpp::module";
    case CppEmitMode::Inspect:
        return "std::cpp::inspect";
    case CppEmitMode::Bench:
        return "std::cpp::bench";
    default:
        return "std::cpp";
    }
}

} // namespace

camel::runtime::GCGraph *CppDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    ASSERT(graph != nullptr, "C++ dump requires a non-null runtime root graph.");

    // Where the arguments of a Camel `main` with parameters come from in the C++ program is
    // not written yet (docs/design/cpp-standalone/README.cn.md, 7.3 and question Q7).
    if (entryTakesParameters(graph) &&
        (mode_ == CppEmitMode::Plain || mode_ == CppEmitMode::Bench)) {
        auto diag =
            context_->rtmDiags()->of(camel::core::error::RuntimeDiag::TranslationRefused);
        if (const uint64_t origin = camel::execute::entryFunctionOrigin(*context_);
            origin != camel::source::kInvalidOriginId) {
            diag.atOrigin(origin);
        }
        diag.commit(
            passName(mode_),
            "'main' takes parameters, and std::cpp does not translate the parameters of the "
            "entry function yet (the C++ program it writes calls 'main' with no arguments)");
        return nullptr;
    }

    camel::passes::cpp::ProgramEmitter emitter(context_, mode_);
    try {
        os << emitter.emit(graph);
    } catch (const camel::passes::cpp::Refusal &refusal) {
        // A program outside what the pass translates is refused where the construct is
        // written: at the node, else at the function it is in, else at the entry function.
        uint64_t origin = camel::source::kInvalidOriginId;
        if (!refusal.aboutFunction && refusal.node != camel::runtime::kInvalidNodeRef) {
            origin = camel::passes::cpp::nodeOriginNear(refusal.graph, refusal.node);
        }
        if (origin == camel::source::kInvalidOriginId) {
            origin = camel::passes::cpp::graphOriginOf(*context_, refusal.graph);
        }
        if (origin == camel::source::kInvalidOriginId) {
            origin = camel::execute::entryFunctionOrigin(*context_);
        }
        auto diag = context_->rtmDiags()->of(camel::core::error::RuntimeDiag::TranslationRefused);
        if (origin != camel::source::kInvalidOriginId) {
            diag.atOrigin(origin);
        }
        diag.commit(passName(mode_), refusal.reason);
    }
    return nullptr;
}
