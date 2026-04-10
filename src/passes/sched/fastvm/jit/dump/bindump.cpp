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
 * Updated: Apr. 10, 2026
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

using namespace GIR;
using namespace camel::core::context;
#if ENABLE_FASTVM_JIT
using namespace camel::jit;
#endif

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

camel::runtime::GCGraph *
JitBinaryDumpPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
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
        os << "[JIT] Backend not available, cannot dump machine code.\n";
        return nullptr;
    }

    os << "[JIT Machine Code] offset (hex bytes) ; entry @ entryOffset\n";
    os << "---\n";

    std::span<const Bytecode> bcSpan(linked.codes.data(), linked.codes.size());

    for (const auto &[runtimeGraph, entryPc] : linked.offsetMap) {
        ASSERT(runtimeGraph != nullptr, "JIT machine-code dump requires a runtime graph.");
        ASSERT(
            runtimeGraph->hasFrameLayout(),
            std::format("Graph '{}' has no finalized frame layout.", runtimeGraph->name()));

        std::vector<std::tuple<size_t, size_t, std::string>> instructionBoundaries;
        CompilationDebugOptions debugOptions{
            .instructionBoundaries = &instructionBoundaries,
            .enableDebugTrace      = true,
        };
        CompilationUnit unit{
            .runtimeGraph       = runtimeGraph,
            .bytecodes          = bcSpan,
            .entryPc            = entryPc,
            .trampolineFunc     = reinterpret_cast<void *>(&trampolineFunc),
            .trampolineTail     = reinterpret_cast<void *>(&trampolineTail),
            .trampolineOper     = reinterpret_cast<void *>(&trampolineOper),
            .trampolineCast     = reinterpret_cast<void *>(&trampolineCast),
            .trampolineBytecode = reinterpret_cast<void *>(&trampolineBytecode),
            .debug              = &debugOptions,
        };

        std::string failureReason;
        auto compiled = backend->compile(unit, &failureReason);
        if (!compiled) {
            os << runtimeGraph->mangledName() << ":\n  [compile failed] "
               << (failureReason.empty() ? "(unknown)" : failureReason) << "\n\n";
            continue;
        }

        size_t codeSize = compiled->code.size();
        // Offset is decimal, and the width is computed from the largest offset's decimal digit
        // count.
        size_t maxOffset = codeSize > 0 ? codeSize - 1 : 0;
        int idxWidth     = 1;
        for (size_t v = maxOffset; v; v /= 10)
            ++idxWidth;

        os << runtimeGraph->mangledName() << ":\n";
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

    return nullptr;
#else
    (void)graph;
    os << "[JIT] JIT not enabled (ENABLE_FASTVM_JIT=0), cannot dump machine code.\n";
    return nullptr;
#endif
}
