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
 * Created: Feb. 08, 2026
 * Updated: Mar. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

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

using namespace GIR;
using namespace camel::core::context;
#if ENABLE_FASTVM_JIT
using namespace camel::jit;
#endif

#if ENABLE_FASTVM_JIT
static graph_ptr_t applyMirDump(
    const context_ptr_t &context, GIR::graph_ptr_t &graph, std::ostream &os, bool slotOnly) {
    const auto &[bytecodes, _, offsetMap] = compileAndLink(
        context,
        graph.get(),
        {
            .enableTailCallDetection = true,
            .enableInlineOperators   = true,
            .optimizationStrategies  = OptimizationStrategyCode::All,
        });

    auto backend = createBackend();
    if (!backend) {
        os << "[JIT] Backend not available, cannot dump MIR.\n";
        return Graph::null();
    }

    os << "[JIT MIR]  [pc][idx]  instruction  ; symbol/slot\n";
    os << "---\n";

    std::span<const Bytecode> bcSpan(bytecodes.data(), bytecodes.size());
    std::unordered_map<uint64_t, std::string> mirSymbolNames;

    for (const auto &[g, entryPc] : offsetMap) {
        FrameMeta *meta = g->frameMeta();
        ASSERT(meta != nullptr, std::format("Graph '{}' has no frozen FrameMeta.", g->name()));

        mirSymbolNames.clear();
        mirSymbolNames[reinterpret_cast<uint64_t>(&trampolineFunc)] = "trampolineFunc";
        mirSymbolNames[reinterpret_cast<uint64_t>(&trampolineTail)] = "trampolineTail";
        mirSymbolNames[reinterpret_cast<uint64_t>(&trampolineOper)] = "trampolineOper";
        mirSymbolNames[reinterpret_cast<uint64_t>(&trampolineCast)] = "trampolineCast";

        CompilationDebugOptions debugOptions{
            .mirOut           = &os,
            .mirSlotOnly      = slotOnly,
            .enableDebugTrace = true,
            .mirSymbolNames   = &mirSymbolNames,
            .mirSlotNames     = nullptr,
        };
        static uint64_t dummyPoolTop = 0;
        CompilationUnit unit{
            .graph                    = g,
            .frameMeta                = meta,
            .bytecodes                = bcSpan,
            .entryPc                  = entryPc,
            .trampolineFunc           = reinterpret_cast<void *>(&trampolineFunc),
            .trampolineTail           = reinterpret_cast<void *>(&trampolineTail),
            .trampolineOper           = reinterpret_cast<void *>(&trampolineOper),
            .trampolineCast           = reinterpret_cast<void *>(&trampolineCast),
            .poolTopAddr              = &dummyPoolTop,
            .directSelfFuncInvokeAddr = reinterpret_cast<void *>(&directSelfFuncInvoke),
            .debug                    = &debugOptions,
        };

        os << "\n" << g->mangledName() << ":\n";
        std::string failureReason;
        if (!backend->compile(unit, &failureReason)) {
            os << "  [compile failed] " << (failureReason.empty() ? "(unknown)" : failureReason)
               << "\n\n";
            continue;
        }
        os << "\n";
    }
    return Graph::null();
}
#endif

graph_ptr_t JitRmirDumpPass::apply(graph_ptr_t &graph, std::ostream &os) {
#if ENABLE_FASTVM_JIT
    return applyMirDump(context_, graph, os, true);
#else
    (void)graph;
    os << "[JIT] JIT not enabled (ENABLE_FASTVM_JIT=0), cannot dump rmir.\n";
    return Graph::null();
#endif
}

graph_ptr_t JitMirDumpPass::apply(graph_ptr_t &graph, std::ostream &os) {
#if ENABLE_FASTVM_JIT
    return applyMirDump(context_, graph, os, false);
#else
    (void)graph;
    os << "[JIT] JIT not enabled (ENABLE_FASTVM_JIT=0), cannot dump MIR.\n";
    return Graph::null();
#endif
}
