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
#include <iomanip>
#include <iostream>

#include "antlr/OpenCMLLexer.h"
#include "antlr/OpenCMLParser.h"
#include "antlr4-runtime.h"

#include "config.h"
#include "core/error/json.h"
#include "core/struct/type.h"
#include "utils/log.h"
#include "visit/ast.h"
#include "visit/cst.h"
#include "visit/fmt.h"
#include "visit/gir.h"

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

            try {
                tree = parser.program();
            } catch (std::exception &e) {
                error << "Parse failed" << std::endl;
                return 1;
            }
        } catch (std::exception &e) {
            error << "Parse failed" << std::endl;
            return 1;
        }

        if (dumpTokens) {
            for (auto &token : tokens.getTokens()) {
                os << std::setw(4) << std::right << token->getTokenIndex() << " [" << std::setw(3) << std::right
                   << token->getLine() << ":" << std::setw(3) << std::left << token->getCharPositionInLine() << "] ("
                   << token->getChannel() << ") : " << token->getText() << std::endl;
            }
            return 0;
        }

        if (format) {
            auto formatter = Formatter(tokens.getTokens());

            const std::string formattedCode = std::any_cast<std::string>(formatter.visit(tree));

            os << formattedCode;
            return 0;
        }

        if (dumpCST) {
            auto visitor = CSTDumpVisitor();
            visitor.visit(tree);
            return 0;
        }

        initTypes();
        ast_ptr_t ast = nullptr;
        auto visitor = ASTConstructor();
        try {
            ast = visitor.construct(tree);
        } catch (BuildException &e) {
            if (errorFormat != "json") {
                error << e.what() << std::endl;
                return 1;
            } else {
                os << e.json() << std::endl;
                return 0;
            }
        } catch (std::exception &e) {
            if (errorFormat != "json") {
                error << "AST construction failed: " << e.what() << std::endl;
                return 1;
            } else {
                os << "{"
                   << "\"filename\": \"" << targetFile << "\", "
                   << "\"line\": 0, "
                   << "\"column\": 0, "
                   << "\"message\": \"AST construction failed: " << e.what() << "\""
                   << "}" << std::endl;
                return 0;
            }
        }

        if (dumpAST) {
            ast->print();
            return 0;
        }

        if (dumpGIR) {
            _dumpGIR();
            return 0;
        }

        if (profile) {
            endTime = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();
            info << "Time used " << duration << " us" << std::endl;
        }
    }

    return 0;
}