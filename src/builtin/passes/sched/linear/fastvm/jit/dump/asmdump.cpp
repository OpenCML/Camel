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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "asmdump.h"
#include "../../compile.h"
#include "../jit_config.h"

#if ENABLE_FASTVM_JIT
#include "../../bytecode.h"
#include "../backend/backend.h"
#include "../runtime/trampoline.h"
#include "core/context/frame.h"
#endif

using namespace GraphIR;

graph_ptr_t JitAsmDumpPass::apply(graph_ptr_t &graph, std::ostream &os) {
#if ENABLE_FASTVM_JIT
    const auto &[bytecodes, _, offsetMap] = compileAndLink(
        context_,
        graph.get(),
        {
            .enableTailCallDetection = true,
            .enableInlineOperators   = true,
            .optimizationStrategies  = OptimizationStrategyCode::All,
        });

    auto backend = camel::jit::createBackend();
    if (!backend) {
        os << "[JIT] Backend not available, cannot dump assembly.\n";
        return Graph::null();
    }

    os << "[JIT Assembly] [offset]  instruction\n";
    os << "---\n";

    std::span<const Bytecode> bcSpan(bytecodes.data(), bytecodes.size());

    for (const auto &[g, entryPc] : offsetMap) {
        FrameMeta *meta = g->getExtra<FrameMeta, 0>();
        if (!meta)
            meta = installFrameMetaInfoForGraph(g);

        camel::jit::CompilationUnit unit{
            .graph          = g,
            .frameMeta      = meta,
            .bytecodes      = bcSpan,
            .entryPc        = entryPc,
            .trampolineFunc = reinterpret_cast<void *>(&trampolineFunc),
            .trampolineTail = reinterpret_cast<void *>(&trampolineTail),
            .trampolineOper = reinterpret_cast<void *>(&trampolineOper),
            .asmOut         = &os,
        };

        os << g->mangledName() << ":\n";
        std::string failureReason;
        auto compiled = backend->compile(unit, &failureReason);
        if (!compiled) {
            os << "  [compile failed] " << (failureReason.empty() ? "(unknown)" : failureReason)
               << "\n\n";
            continue;
        }
        os << "\n";
    }

    return Graph::null();
#else
    (void)graph;
    os << "[JIT] JIT not enabled (ENABLE_FASTVM_JIT=0), cannot dump assembly.\n";
    return Graph::null();
#endif
}
