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
#include "camel/runtime/loop_lowering.h"

#include "camel/core/error/diagnostics.h"
#include "camel/core/source/manager.h"
#include "camel/execute/entry_args.h"

namespace {

using camel::passes::cpp::CppEmitMode;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;

const char *passName(CppEmitMode mode) {
    switch (mode) {
    case CppEmitMode::Module:
        return "std::cpp::module";
    case CppEmitMode::Inspect:
        return "std::cpp::inspect";
    default:
        return "std::cpp";
    }
}

} // namespace

const camel::runtime::Program *
CppDumpPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    ASSERT(graph != nullptr, "C++ dump requires a non-null runtime root graph.");
    // A loop is emitted as the tail recursion it means (camel/runtime/loop_lowering.h), on a
    // private copy of the program; the generated tail calls do not grow the stack.
    const camel::runtime::Program *lowered =
        camel::runtime::lowerLoopsToTailRecursion(context_, program);
    graph = lowered->root();

    camel::passes::cpp::ProgramEmitter emitter(context_, mode_);
    try {
        os << emitter.emit(*lowered, graph);
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
