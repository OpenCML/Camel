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
 * Created: Feb. 07, 2026
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "asmdump.h"
#include "../../compile.h"
#include "../jit_config.h"

#if ENABLE_FASTVM_JIT
#include "../../bytecode.h"
#include "../backend/backend.h"
#include "../runtime/trampoline.h"
#include "camel/core/context/frame.h"
#endif

using namespace GIR;
using namespace camel::core::context;
#if ENABLE_FASTVM_JIT
using namespace camel::jit;
#endif

camel::runtime::GCGraph *JitAsmDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
#if ENABLE_FASTVM_JIT
    auto linked = compileAndLink(
        context_,
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

    for (const auto &[runtimeGraph, entryPc] : linked.offsetMap) {
        ASSERT(runtimeGraph != nullptr, "JIT assembly dump requires a runtime graph.");
        ASSERT(
            runtimeGraph->hasFrameLayout(),
            std::format("Graph '{}' has no finalized frame layout.", runtimeGraph->name()));

        CompilationDebugOptions debugOptions{
            .asmOut           = &os,
            .enableDebugTrace = true,
        };
        static uint64_t dummyPoolTop = 0;
        CompilationUnit unit{
            .runtimeGraph             = runtimeGraph,
            .bytecodes                = bcSpan,
            .entryPc                  = entryPc,
            .trampolineFunc           = reinterpret_cast<void *>(&trampolineFunc),
            .trampolineTail           = reinterpret_cast<void *>(&trampolineTail),
            .trampolineOper           = reinterpret_cast<void *>(&trampolineOper),
            .trampolineCast           = reinterpret_cast<void *>(&trampolineCast),
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
