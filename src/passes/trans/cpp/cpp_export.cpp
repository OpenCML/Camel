// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "cpp_export.h"

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
    camel::passes::cpp::ProgramEmitter emitter(context_, mode_);
    try {
        os << emitter.emit(*program, graph);
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
