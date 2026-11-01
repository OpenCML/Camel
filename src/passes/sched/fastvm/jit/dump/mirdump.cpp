// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "mirdump.h"
#include "../../compile.h"
#include "../jit_config.h"

#if ENABLE_FASTVM_JIT
#include "../../bytecode.h"
#include "../backend/backend.h"
#include "../runtime/trampoline.h"
#include "camel/core/context/frame.h"
#include <unordered_map>
#endif

using namespace camel::core::context;
#if ENABLE_FASTVM_JIT
using namespace camel::jit;
#endif

#if ENABLE_FASTVM_JIT
static const camel::runtime::Program *applyMirDump(
    const context_ptr_t &context, const camel::runtime::Program &program,
    camel::runtime::GCGraph *graph, std::ostream &os, bool slotOnly) {
    auto linked = compileAndLink(
        context,
        program,
        graph,
        {
            .enableTailCallDetection = true,
            .enableInlineOperators   = true,
            .optimizationStrategies  = OptimizationStrategyCode::All,
        });

    auto backend = createBackend();
    if (!backend) {
        os << "[JIT] Backend not available, cannot dump MIR.\n";
        return nullptr;
    }

    os << "[JIT MIR]  [pc][idx]  instruction  ; symbol/slot\n";
    os << "---\n";

    std::span<const Bytecode> bcSpan(linked.codes.data(), linked.codes.size());
    std::unordered_map<uint64_t, std::string> mirSymbolNames;

    std::unordered_map<camel::runtime::GCGraph *, size_t> graphLengths;
    graphLengths.reserve(linked.graphs.size());
    for (const auto &[offset, length, runtimeGraph] : linked.graphs) {
        (void)offset;
        if (runtimeGraph) {
            graphLengths.emplace(runtimeGraph, length);
        }
    }

    for (const auto &[runtimeGraph, entryPc] : linked.offsetMap) {
        GUARD(runtimeGraph != nullptr, "JIT MIR dump requires a runtime graph.");
        GUARD(
            runtimeGraph->hasFrameLayout(),
            std::format("Graph '{}' has no finalized frame layout.", runtimeGraph->name()));

        mirSymbolNames.clear();
        mirSymbolNames[reinterpret_cast<uint64_t>(&trampolineFunc)]     = "trampolineFunc";
        mirSymbolNames[reinterpret_cast<uint64_t>(&trampolineTail)]     = "trampolineTail";
        mirSymbolNames[reinterpret_cast<uint64_t>(&trampolineOper)]     = "trampolineOper";
        mirSymbolNames[reinterpret_cast<uint64_t>(&trampolineCast)]     = "trampolineCast";
        mirSymbolNames[reinterpret_cast<uint64_t>(&trampolineBytecode)] = "trampolineBytecode";

        CompilationDebugOptions debugOptions{
            .mirOut           = &os,
            .mirSlotOnly      = slotOnly,
            .enableDebugTrace = true,
            .mirSymbolNames   = &mirSymbolNames,
            .mirSlotNames     = nullptr,
        };
        static uint64_t dummyPoolTop = 0;
        CompilationUnit unit{
            .runtimeGraph   = runtimeGraph,
            .bytecodes      = bcSpan,
            .entryPc        = entryPc,
            .graphLength    = graphLengths.contains(runtimeGraph) ? graphLengths.at(runtimeGraph)
                                                                  : static_cast<size_t>(0),
            .trampolineFunc = reinterpret_cast<void *>(&trampolineFunc),
            .trampolineTail = reinterpret_cast<void *>(&trampolineTail),
            .trampolineOper = reinterpret_cast<void *>(&trampolineOper),
            .trampolineCast = reinterpret_cast<void *>(&trampolineCast),
            .trampolineBytecode       = reinterpret_cast<void *>(&trampolineBytecode),
            .poolTopAddr              = &dummyPoolTop,
            .directSelfFuncInvokeAddr = reinterpret_cast<void *>(&directSelfFuncInvoke),
            .debug                    = &debugOptions,
        };

        os << "\n" << runtimeGraph->mangledName() << ":\n";
        std::string failureReason;
        if (!backend->compile(unit, &failureReason)) {
            os << "  [compile failed] " << (failureReason.empty() ? "(unknown)" : failureReason)
               << "\n\n";
            continue;
        }
        os << "\n";
    }
    return nullptr;
}
#endif

const camel::runtime::Program *
JitRmirDumpPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
#if ENABLE_FASTVM_JIT
    return applyMirDump(context_, *program, graph, os, true);
#else
    (void)graph;
    os << "[JIT] JIT not enabled (ENABLE_FASTVM_JIT=0), cannot dump rmir.\n";
    return nullptr;
#endif
}

const camel::runtime::Program *
JitMirDumpPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
#if ENABLE_FASTVM_JIT
    return applyMirDump(context_, *program, graph, os, false);
#else
    (void)graph;
    os << "[JIT] JIT not enabled (ENABLE_FASTVM_JIT=0), cannot dump MIR.\n";
    return nullptr;
#endif
}
