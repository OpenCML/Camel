// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "asmdump.h"
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

const camel::runtime::Program *
JitAsmDumpPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
#if ENABLE_FASTVM_JIT
    auto linked = compileAndLink(
        context_,
        *program,
        graph,
        {
            .enableTailCallDetection = true,
            .enableInlineOperators   = true,
            .optimizationStrategies  = OptimizationStrategyCode::All,
        });

    auto backend = createBackend();
    if (!backend) {
        os << "[JIT] Backend not available, cannot dump assembly.\n";
        return nullptr;
    }

    os << "[JIT Assembly] [offset]  instruction\n";
    os << "---\n";

    std::span<const Bytecode> bcSpan(linked.codes.data(), linked.codes.size());

    // The backend compiles one graph's own bytecode: it needs the length of each graph.
    std::unordered_map<camel::runtime::GCGraph *, size_t> graphLengths;
    for (const auto &[offset, length, runtimeGraph] : linked.graphs) {
        (void)offset;
        if (runtimeGraph) {
            graphLengths.emplace(runtimeGraph, length);
        }
    }

    for (const auto &[runtimeGraph, entryPc] : linked.offsetMap) {
        GUARD(runtimeGraph != nullptr, "JIT assembly dump requires a runtime graph.");
        GUARD(
            runtimeGraph->hasFrameLayout(),
            std::format("Graph '{}' has no finalized frame layout.", runtimeGraph->name()));

        CompilationDebugOptions debugOptions{
            .asmOut           = &os,
            .enableDebugTrace = true,
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

        os << runtimeGraph->mangledName() << ":\n";
        std::string failureReason;
        auto compiled = backend->compile(unit, &failureReason);
        if (!compiled) {
            os << "  [compile failed] " << (failureReason.empty() ? "(unknown)" : failureReason)
               << "\n\n";
            continue;
        }
        os << "\n";
    }

    return nullptr;
#else
    (void)graph;
    os << "[JIT] JIT not enabled (ENABLE_FASTVM_JIT=0), cannot dump assembly.\n";
    return nullptr;
#endif
}
