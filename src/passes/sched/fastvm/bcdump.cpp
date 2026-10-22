// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "bcdump.h"

#include "camel/runtime/reachable.h"
#include "compile.h"

const camel::runtime::Program *
BytecodeDumpPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    std::vector<camel::runtime::GCGraph *> allGraphs =
        camel::runtime::collectReachableGraphs(*program, graph);

    os << "[index] opcode (opsize) [self] | [fastops] | <with> (norm) | extra\n";

    for (auto *g : allGraphs) {
        auto bytecodes = compile(
            context_,
            *program,
            g,
            {
                .enableTailCallDetection = true,
                .enableInlineOperators   = true,
                .optimizationStrategies  = OptimizationStrategyCode::All,
            });
        os << g->mangledName() << ":\n";

        int maxwidth = computeWidth(bytecodes.size());

        for (size_t i = 0; i < bytecodes.size();) {
            os << "  [" << formatIndex(i, maxwidth) << "] ";
            os << opCodeToString(bytecodes[i], context_) << "\n";
            i += bytecodes[i].opsize;
        }

        os << std::format("  [used: {}, allocated: {}]\n", bytecodes.size(), bytecodes.capacity());
    }

    return nullptr;
}

const camel::runtime::Program *
LinkedBytecodeDumpPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    camel::runtime::GCGraph *graph = program->root();
    auto linked                    = compileAndLink(
        context_,
        *program,
        graph,
        {
                               .enableTailCallDetection = true,
                               .enableInlineOperators   = true,
                               .optimizationStrategies  = OptimizationStrategyCode::All,
        });

    os << "[index] opcode (opsize) [self] | [fastops] | <with> (norm) | extra\n";

    int maxwidth = computeWidth(linked.codes.size());

    for (const auto &[offset, length, runtimeGraph] : linked.graphs) {
        ASSERT(runtimeGraph != nullptr, "Linked bytecode entry is missing runtime graph metadata.");
        os << runtimeGraph->mangledName() << ":\n";
        for (size_t i = offset; i < offset + length;) {
            os << "  [" << formatIndex(i, maxwidth) << "] ";
            os << opCodeToString(linked.codes[i], context_) << "\n";
            i += linked.codes[i].opsize;
        }
    }

    return nullptr;
}
