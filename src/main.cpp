/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Sep. 01, 2023
 * Supported by: National Key Research and Development Program of China
 */

#include <chrono>
#include <iostream>

#include "antlr/OpenCMLLexer.h"
#include "antlr/OpenCMLParser.h"
#include "antlr4-runtime.h"

#include "config.h"

#include "dump/visitor.h"

using namespace antlr4;

int main(int argc, char *argv[]) {
    std::chrono::high_resolution_clock::time_point startTime, endTime;

    if (!parseArgs(argc, argv)) {
        return 0;
    }

    if (profile) {
        std::cout << "Profiling mode" << std::endl;
        for (int i = 0; i < 10; i++) {
            startTime = std::chrono::high_resolution_clock::now();
            auto src = std::ifstream();
            src.open(targetFile);
            ANTLRInputStream input(src);
            OpenCMLLexer lexer(&input);
            CommonTokenStream tokens(&lexer);
            OpenCMLParser parser(&tokens);
            parser.getInterpreter<atn::ParserATNSimulator>()->setPredictionMode(
                atn::PredictionMode::SLL);
            parser.setErrorHandler(std::make_shared<BailErrorStrategy>());
            // parser.removeErrorListeners();
            std::cout << "Parsing " << targetFile << "..." << std::endl;
            try {
                tree::ParseTree *tree = parser.program();
            } catch (ParseCancellationException &e) {
                std::cout << "Parse cancelled" << std::endl;
                parser.reset();
                tokens.reset();
                // parser.addErrorListener(new ProxyErrorListener());
                parser.getInterpreter<atn::ParserATNSimulator>()->setPredictionMode(
                    atn::PredictionMode::LL);
                tree::ParseTree *tree = parser.program();
            }
            std::cout << "Parsed " << targetFile << "..." << std::endl;
            endTime = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(endTime -
                                                                      startTime)
                    .count();
            std::cout << "Time used " << duration << " ms" << std::endl;
        }
        return 0;
    }

    auto src = std::ifstream();
    src.open(targetFile);
    ANTLRInputStream input(src);
    OpenCMLLexer lexer(&input);
    CommonTokenStream tokens(&lexer);
    OpenCMLParser parser(&tokens);
    tree::ParseTree *tree = parser.program();

    if (dumpCST) {
        auto visitor = CSTDumpVisitor();
        visitor.visit(tree);
        return 0;
    }

    return 0;
}