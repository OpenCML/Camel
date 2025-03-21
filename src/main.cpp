/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: Sep. 01, 2023
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <chrono>
#include <iomanip>
#include <iostream>

#include "nlohmann/json.hpp"

#include "antlr/OpenCMLLexer.h"
#include "antlr/OpenCMLParser.h"
#include "antlr4-runtime/antlr4-runtime.h"

#include "codegen/girdump/graphviz.h"
#include "common/error/error.h"
#include "common/error/json.h"
#include "common/type.h"
#include "compile/parse/cst.h"
#include "compile/parse/gct.h"
#include "compile/parse/gir.h"
#include "config.h"
#include "operators/init.h"
#include "service/formatter/fmt.h"
#include "utils/log.h"

using namespace antlr4;
using namespace std;

using namespace CLI;

#define DEBUG_LEVEL -1

int main(int argc, char *argv[]) {
    if (!parseArgs(argc, argv))
        return 0;

    ostream &os = cout;

    chrono::high_resolution_clock::time_point startTime, endTime;

    while (repeat--) {
        bool hasParseError = false;

        if (profile) {
            startTime = chrono::high_resolution_clock::now();
        }

        ANTLRInputStream input;
        string targetFile = "stdin";

        if (!targetFiles.empty()) {
            targetFile = targetFiles[0];
            auto src = ifstream();
            src.open(targetFile);
            if (!src.is_open()) {
                error << "Error opening file " << targetFile << endl;
                return 1;
            }
            input = ANTLRInputStream(src);
        } else {
            input = ANTLRInputStream(cin);
        }

        OpenCMLLexer lexer(&input);
        CommonTokenStream tokens(&lexer);

        if (Inspect::dumpTokens) {
            while (true) {
                Token *token = tokens.LT(1);
                if (token->getType() == Token::EOF) {
                    break;
                }
                os << setw(4) << right << token->getTokenIndex() << " [" << setw(3) << right << token->getLine() << ":"
                   << setw(3) << left << token->getCharPositionInLine() << "] (" << token->getChannel()
                   << ") : " << token->getText() << endl;
                tokens.consume();
            }
            tokens.reset();
        }

        OpenCMLParser parser(&tokens);
        auto interpreter = parser.getInterpreter<atn::ParserATNSimulator>();
        tree::ParseTree *tree = nullptr;
        parser.removeErrorListeners();

        try {
            interpreter->setPredictionMode(atn::PredictionMode::SLL);
            parser.setErrorHandler(make_shared<BailErrorStrategy>());
            tree = parser.program();
        } catch (ParseCancellationException &e) {
            debug(1) << "Parse failed, retrying with LL mode" << endl;

            CamelErrorListener *listener = nullptr;

            if (errorFormat == "text") {
                listener = new CamelErrorListener(targetFile, os);
            } else if (errorFormat == "json") {
                listener = new JSONErrorListener(targetFile, os);
            } else {
                error << "Unknown error format: " << errorFormat << endl;
                return 1;
            }

            parser.addErrorListener(listener);

            parser.reset();
            tokens.reset();
            interpreter->setPredictionMode(atn::PredictionMode::LL);
            parser.setErrorHandler(make_shared<DefaultErrorStrategy>());

            try {
                tree = parser.program();
            } catch (exception &e) {
                error << "Parse failed" << endl;
                return 1;
            }

            hasParseError = listener->hasErrors();
        } catch (exception &e) {
            error << "Parse failed" << endl;
            return 1;
        }

        if (Format::formatCode && !hasParseError) {
            auto formatter = Formatter(tokens.getTokens());
            const string formattedCode = any_cast<string>(formatter.visit(tree));
            os << formattedCode;
        }

        if (Inspect::dumpCST) {
            auto cstDumpVisitor = CSTDumpVisitor(os);
            cstDumpVisitor.visit(tree);
        }

        if (selectedCommand == Command::INSPECT && !hasParseError) {
            initTypes();
            GCT::node_ptr_t gct = nullptr;
            auto astConstructor = GCT::Constructor();
            try {
                gct = astConstructor.construct(tree);
                auto &warns = astConstructor.warns();
                while (!warns.empty()) {
                    const auto &warning = warns.front();
                    if (errorFormat != "json") {
                        error << warning.what() << endl;
                    } else {
                        os << warning.json() << endl;
                    }
                    warns.pop();
                }
            } catch (BuildException &e) {
                if (errorFormat != "json") {
                    error << e.what() << endl;
                    return 1;
                } else {
                    os << e.json() << endl;
                    return 0;
                }
            } catch (exception &e) {
                if (errorFormat != "json") {
                    error << "GCT construction failed: " << e.what() << endl;
                    return 1;
                } else {
                    os << "{"
                       << "\"type\": \"error\", "
                       << "\"filename\": \"" << targetFile << "\", "
                       << "\"line\": 0, "
                       << "\"column\": 0, "
                       << "\"message\": \"GCT construction failed: " << e.what() << "\""
                       << "}" << endl;
                    return 0;
                }
            }

            if (Inspect::dumpAST && gct) {
                // currently we do not have AST, print GCT instead
                gct->print(os);
            }

            if (Inspect::dumpGCT && gct) {
                gct->print(os);
            }

            GIR::graph_ptr_t gir = nullptr;
            context_ptr_t ctx = make_shared<Context>();

            if (Inspect::dumpGIR) {
                initOperators();
                auto girConstructor = GIR::Constructor(ctx);
                try {
                    gir = girConstructor.construct(gct);
                } catch (exception &e) {
                    error << "GIR construction failed: " << e.what() << endl;
                    return 1;
                }
            }

            if (Inspect::dumpGIR && gir) {
                GraphVizDumpPass pass(ctx);
                auto res = pass.apply(gir);
                os << any_cast<string>(res);
            }
        }

        if (profile) {
            endTime = chrono::high_resolution_clock::now();
            auto duration = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
            info << "Time used " << duration << " us" << endl;
        }
    }

    return 0;
}
