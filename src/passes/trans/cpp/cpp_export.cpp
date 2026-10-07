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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "cpp_export.h"

#include "runtime_emitter.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"
#include "camel/execute/entry_args.h"

namespace {

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

/// The source origin of `ref`, or of the nearest input that has one (a node the compiler
/// synthesized carries none; what it reads does).
uint64_t nodeOriginNear(const GCGraph *g, camel::runtime::gc_node_ref_t ref) {
    if (const uint64_t origin = g->nodeOrigin(ref); origin != camel::source::kInvalidOriginId) {
        return origin;
    }
    for (const auto inputs : {g->withInputsOf(ref), g->normInputsOf(ref)}) {
        for (camel::runtime::gc_node_ref_t input : inputs) {
            if (const uint64_t origin = g->nodeOrigin(input);
                origin != camel::source::kInvalidOriginId) {
                return origin;
            }
        }
    }
    return camel::source::kInvalidOriginId;
}

/// The definition of the function `g` was compiled from, or the first of its nodes that has a
/// source position.
uint64_t graphOriginOf(const camel::core::context::Context &ctx, const GCGraph *g) {
    if (g == nullptr) {
        return camel::source::kInvalidOriginId;
    }
    if (const auto source = ctx.sourceContext()) {
        const auto origin = source->debugMap().graphOrigin(g->stableId());
        if (origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }
    for (auto it = g->nodes().begin(); it != g->nodes().end(); ++it) {
        if (*it == nullptr) {
            continue;
        }
        if (const uint64_t origin = g->nodeOrigin(it.ref());
            origin != camel::source::kInvalidOriginId) {
            return origin;
        }
    }
    return camel::source::kInvalidOriginId;
}

} // namespace

camel::runtime::GCGraph *CppDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    ASSERT(graph != nullptr, "C++ dump requires a non-null runtime root graph.");

    // The generated program's `int main()` calls the entry function with nothing; where the
    // arguments of a Camel `main` with parameters would come from in the C++ program is not
    // defined yet.
    if (entryTakesParameters(graph)) {
        auto diag =
            context_->rtmDiags()->of(camel::core::error::RuntimeDiag::TranslationRefused);
        if (const uint64_t origin = camel::execute::entryFunctionOrigin(*context_);
            origin != camel::source::kInvalidOriginId) {
            diag.atOrigin(origin);
        }
        diag.commit(
            "std::cpp",
            "'main' takes parameters, and std::cpp does not translate the parameters of the "
            "entry function yet (the C++ program it writes calls 'main' with no arguments)");
        return nullptr;
    }

    RuntimeCppEmitter emitter(context_, mode_);
    try {
        os << emitter.emit(graph);
    } catch (const CppTranslationRefusal &refusal) {
        // A program outside what the pass translates is refused where the construct is
        // written: at the node, else at the function it is in, else at the entry function.
        const auto &issue = refusal.issue;
        uint64_t origin   = camel::source::kInvalidOriginId;
        if (!issue.aboutFunction && issue.nodeRef != camel::runtime::kInvalidNodeRef) {
            origin = nodeOriginNear(issue.graph, issue.nodeRef);
        }
        if (origin == camel::source::kInvalidOriginId) {
            origin = graphOriginOf(*context_, issue.graph);
        }
        if (origin == camel::source::kInvalidOriginId) {
            origin = camel::execute::entryFunctionOrigin(*context_);
        }
        auto diag = context_->rtmDiags()->of(camel::core::error::RuntimeDiag::TranslationRefused);
        if (origin != camel::source::kInvalidOriginId) {
            diag.atOrigin(origin);
        }
        diag.commit(
            mode_ == CppEmitMode::Module    ? "std::cpp::module"
            : mode_ == CppEmitMode::Inspect ? "std::cpp::inspect"
            : mode_ == CppEmitMode::Bench   ? "std::cpp::bench"
                                            : "std::cpp",
            issue.reason);
    }
    return nullptr;
}
