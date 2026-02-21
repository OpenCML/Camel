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
 * Created: Feb. 06, 2026
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "bindump.h"
#include "../../compile.h"
#include "../jit_config.h"

#if ENABLE_FASTVM_JIT
#include "../../bytecode.h"
#include "../backend/backend.h"
#include "../runtime/trampoline.h"
#include "camel/core/context/frame.h"
#endif

#include <iomanip>
#include <sstream>
#include <tuple>
#include <vector>

using namespace GraphIR;

#if ENABLE_FASTVM_JIT
static void dumpMachineCode(
    std::ostream &os, const uint8_t *code, size_t size, size_t entryOffset, int maxWidth) {
    constexpr size_t bytesPerLine = 16;
    for (size_t i = 0; i < size; i += bytesPerLine) {
        std::ostringstream idxStr;
        idxStr << std::setw(maxWidth) << std::setfill(' ') << i;
        os << "  [" << idxStr.str() << "]";

        for (size_t j = 0; j < bytesPerLine && i + j < size; ++j) {
            os << " " << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(code[i + j]);
        }
        os << std::dec;
        if (i <= entryOffset && entryOffset < i + bytesPerLine) {
            os << "  ; entry @ " << entryOffset;
        }
        os << "\n";
    }
}

static void dumpMachineCodeByInstruction(
    std::ostream &os, const uint8_t *code, size_t codeSize, size_t entryOffset, int maxWidth,
    const std::vector<std::tuple<size_t, size_t, std::string>> &boundaries) {
    for (const auto &[start, len, text] : boundaries) {
        if (start + len > codeSize)
            break;
        os << "  [" << std::setw(maxWidth) << std::setfill(' ') << std::dec << start << "]";
        for (size_t j = 0; j < len; ++j)
            os << " " << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(code[start + j]);
        os << std::dec;
        if (start <= entryOffset && entryOffset < start + len)
            os << "  ; entry @ " << entryOffset << "  " << text;
        else
            os << "  ; " << text;
        os << "\n";
    }
}
#endif

graph_ptr_t JitBinaryDumpPass::apply(graph_ptr_t &graph, std::ostream &os) {
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
        os << "[JIT] Backend not available, cannot dump machine code.\n";
        return Graph::null();
    }

    os << "[JIT Machine Code] offset (hex bytes) ; entry @ entryOffset\n";
    os << "---\n";

    std::span<const Bytecode> bcSpan(bytecodes.data(), bytecodes.size());

    for (const auto &[g, entryPc] : offsetMap) {
        FrameMeta *meta = g->getExtra<FrameMeta, 0>();
        if (!meta)
            meta = installFrameMetaInfoForGraph(g);

        std::vector<std::tuple<size_t, size_t, std::string>> instructionBoundaries;
        camel::jit::CompilationUnit unit{
            .graph                 = g,
            .frameMeta             = meta,
            .bytecodes             = bcSpan,
            .entryPc               = entryPc,
            .trampolineFunc        = reinterpret_cast<void *>(&trampolineFunc),
            .trampolineTail        = reinterpret_cast<void *>(&trampolineTail),
            .trampolineOper        = reinterpret_cast<void *>(&trampolineOper),
            .trampolineCast        = reinterpret_cast<void *>(&trampolineCast),
            .instructionBoundaries = &instructionBoundaries,
        };

        std::string failureReason;
        auto compiled = backend->compile(unit, &failureReason);
        if (!compiled) {
            os << g->mangledName() << ":\n  [compile failed] "
               << (failureReason.empty() ? "(unknown)" : failureReason) << "\n\n";
            continue;
        }

        size_t codeSize = compiled->code.size();
        // 偏移为十进制，宽度按最大偏移的十进制位数计算
        size_t maxOffset = codeSize > 0 ? codeSize - 1 : 0;
        int idxWidth     = 1;
        for (size_t v = maxOffset; v; v /= 10)
            ++idxWidth;

        os << g->mangledName() << ":\n";
        os << "  [code size: " << codeSize << " bytes, entryOffset: " << compiled->entryOffset
           << "]\n";

        if (!compiled->code.empty()) {
            if (!instructionBoundaries.empty()) {
                dumpMachineCodeByInstruction(
                    os,
                    compiled->code.data(),
                    compiled->code.size(),
                    compiled->entryOffset,
                    idxWidth,
                    instructionBoundaries);
            } else {
                dumpMachineCode(
                    os,
                    compiled->code.data(),
                    compiled->code.size(),
                    compiled->entryOffset,
                    idxWidth);
            }
        }

        os << "\n";
    }

    return Graph::null();
#else
    (void)graph;
    os << "[JIT] JIT not enabled (ENABLE_FASTVM_JIT=0), cannot dump machine code.\n";
    return Graph::null();
#endif
}
