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
#include "core/error/json.h"
#include "dump/cst.h"
#include "dump/fmt.h"
#include "utils/log.h"

using namespace antlr4;

int main(int argc, char *argv[]) {
    if (!parseArgs(argc, argv))
        return 0;

    std::ostream &os = std::cout;

    std::chrono::high_resolution_clock::time_point startTime, endTime;

    while (repeat--) {
        if (profile) {
            startTime = std::chrono::high_resolution_clock::now();
        }

        ANTLRInputStream input;

        if (targetFile != "") {
            auto src = std::ifstream();
            src.open(targetFile);
            if (!src.is_open()) {
                error << "Error opening file " << targetFile << std::endl;
                return 1;
            }
            input = ANTLRInputStream(src);
        } else {
            input = ANTLRInputStream(std::cin);
        }

        OpenCMLLexer lexer(&input);
        CommonTokenStream tokens(&lexer);
        OpenCMLParser parser(&tokens);
        auto interpreter = parser.getInterpreter<atn::ParserATNSimulator>();
        tree::ParseTree *tree = nullptr;

        try {
            interpreter->setPredictionMode(atn::PredictionMode::SLL);
            parser.setErrorHandler(std::make_shared<BailErrorStrategy>());
            tree = parser.program();
        } catch (ParseCancellationException &e) {
            debug(1) << "Parse failed, retrying with LL mode" << std::endl;

            parser.removeErrorListeners();

            if (errorFormat == "text") {
                parser.addErrorListener(new ConsoleErrorListener());
            } else if (errorFormat == "json") {
                parser.addErrorListener(new JSONErrorListener(targetFile, os));
            } else {
                error << "Unknown error format: " << errorFormat << std::endl;
                return 1;
            }

            parser.reset();
            tokens.reset();
            interpreter->setPredictionMode(atn::PredictionMode::LL);
            parser.setErrorHandler(std::make_shared<DefaultErrorStrategy>());
            tree = parser.program();
        }

        if (dumpCST) {
            auto visitor = CSTDumpVisitor();
            visitor.visit(tree);
        }

        if (format) {
            auto visitor = Formatter();
            const std::string formattedCode = std::any_cast<std::string>(visitor.visit(tree));
            os << formattedCode;
        }

        if (profile) {
            endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
            info << "Time used " << duration << " us" << std::endl;
        }
    }

    return 0;
}