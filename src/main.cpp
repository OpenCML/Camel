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
 * Created: Sep. 01, 2023
 * Updated: Mar. 17, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <chrono>
#include <iomanip>
#include <iostream>

#include "nlohmann/json.hpp"

#include "parse/antlr/OpenCMLLexer.h"
#include "parse/antlr/OpenCMLParser.h"
#include "antlr4-runtime/antlr4-runtime.h"

#include "builtin/passes/girdump/graphviz.h"
#include "common/error/error.h"
#include "common/error/json.h"
#include "common/type.h"
#include "parse/ast.h"
#include "parse/cst-dump.h"
#include "compile/gct.h"
#include "compile/gir.h"
#include "config.h"
#include "builtin/operators/init.h"
#include "service/formatter/fmt.h"
#include "utils/log.h"

using namespace antlr4;
using namespace std;

using namespace CLI;

#define DEBUG_LEVEL -1

string targetFile = "";

void dumpTokens(CommonTokenStream &tokens) {
    while (true) {
        Token *token = tokens.LT(1);
        if (token->getType() == Token::EOF) {
            break;
        }
        cout << setw(4) << right << token->getTokenIndex() << " [" << setw(3) << right << token->getLine() << ":"
             << setw(3) << left << token->getCharPositionInLine() << "] (" << token->getChannel()
             << ") : " << token->getText() << endl;
        tokens.consume();
    }
    tokens.reset();
}

bool buildCST(tree::ParseTree *&cst, OpenCMLParser &parser, ostream &os, string errorFormat) {
    auto interpreter = parser.getInterpreter<atn::ParserATNSimulator>();
    parser.removeErrorListeners();

    try {
        interpreter->setPredictionMode(atn::PredictionMode::SLL);
        parser.setErrorHandler(make_shared<BailErrorStrategy>());
        cst = parser.program();
    } catch (ParseCancellationException &e) {
        debug(1) << "Parse failed, retrying with LL mode." << endl;

        CamelErrorListener *listener = nullptr;

        if (errorFormat == "text") {
            listener = new CamelErrorListener("stdin", os);
        } else if (errorFormat == "json") {
            listener = new JSONErrorListener("stdin", os);
        } else {
            error << "Unknown error format: " << errorFormat << endl;
            return false;
        }

        parser.addErrorListener(listener);

        parser.reset();
        interpreter->setPredictionMode(atn::PredictionMode::LL);
        parser.setErrorHandler(make_shared<DefaultErrorStrategy>());

        try {
            cst = parser.program();
        } catch (exception &e) {
            debug(1) << "Parse failed. " << e.what() << endl;
            return false;
        }

        if (listener->hasErrors()) {
            return false;
        }
    } catch (exception &e) {
        debug(1) << "Parse failed. " << e.what() << endl;
        return false;
    }
    return true;
}

bool buildAST(AST::node_ptr_t &ast, tree::ParseTree *cst, ostream &os, string errorFormat) {
    auto constructor = AST::Constructor();
    try {
        ast = constructor.construct(cst);
        return true;
    } catch (BuildException &e) {
        if (errorFormat != "json") {
            error << e.what() << endl;
        } else {
            os << e.json() << endl;
        }
        return false;
    } catch (exception &e) {
        if (errorFormat != "json") {
            error << "AST construction failed: " << e.what() << endl;
        } else {
            os << "{"
               << "\"type\": \"error\", "
               << "\"filename\": \"" << targetFile << "\", "
               << "\"line\": 0, "
               << "\"column\": 0, "
               << "\"message\": \"AST construction failed: " << e.what() << "\""
               << "}" << endl;
        }
        return false;
    }
}

bool buildGCT(GCT::node_ptr_t &gct, AST::node_ptr_t &ast, ostream &os, string errorFormat) {
    return false; // TODO: build GCT
    initTypes();
    auto constructor = GCT::Constructor();
    try {
        // gct = constructor.construct(ast);
        // TODO: remove this line after GCT is implemented
        auto &warns = constructor.warns();
        if (selectedCommand == Command::CHECK) {
            while (!warns.empty()) {
                const auto &warning = warns.front();
                if (errorFormat != "json") {
                    error << warning.what() << endl;
                } else {
                    os << warning.json() << endl;
                }
                warns.pop();
            }
        }
    } catch (BuildException &e) {
        if (errorFormat != "json") {
            error << e.what() << endl;
            return false;
        } else {
            os << e.json() << endl;
            return false;
        }
    } catch (exception &e) {
        if (errorFormat != "json") {
            error << "GCT construction failed: " << e.what() << endl;
            return false;
        } else {
            os << "{"
               << "\"type\": \"error\", "
               << "\"filename\": \"" << targetFile << "\", "
               << "\"line\": 0, "
               << "\"column\": 0, "
               << "\"message\": \"GCT construction failed: " << e.what() << "\""
               << "}" << endl;
            return false;
        }
    }
    return true;
}

bool buildGIR(GIR::graph_ptr_t &gir, GCT::node_ptr_t &gct, context_ptr_t &ctx, ostream &os, string errorFormat) {
    initOperators();
    auto constructor = GIR::Constructor(ctx);
    try {
        gir = constructor.construct(gct);
    } catch (exception &e) {
        if (errorFormat != "json") {
            error << "GIR construction failed: " << e.what() << endl;
            return false;
        } else {
            os << "{"
               << "\"type\": \"error\", "
               << "\"filename\": \"" << targetFile << "\", "
               << "\"line\": 0, "
               << "\"column\": 0, "
               << "\"message\": \"GIR construction failed: " << e.what() << "\""
               << "}" << endl;
            return false;
        }
    }
    return true;
}

int main(int argc, char *argv[]) {
    if (!parseArgs(argc, argv))
        return 0;

    ostream &os = cout;

    string errorFormat = Run::errorFormat;
    if (selectedCommand == Command::CHECK) {
        errorFormat = Check::outputFormat;
    }

    ANTLRInputStream input;

    if (Run::targetFiles.empty() || Run::targetFiles[0] == "") {
        input = ANTLRInputStream(cin);
        targetFile = "stdin"; // for error reporting
    } else {
        targetFile = Run::targetFiles[0];
        auto src = ifstream();
        src.open(targetFile);
        if (!src.is_open()) {
            error << "Error opening file " << targetFile << endl;
            return 1;
        }
        input = ANTLRInputStream(src);
    }

    chrono::high_resolution_clock::time_point startTime, endTime;

    while (Run::repeat--) {
        if (Run::profile) {
            startTime = chrono::high_resolution_clock::now();
        }

        OpenCMLLexer lexer(&input);
        CommonTokenStream tokens(&lexer);
        OpenCMLParser parser(&tokens);

        if (Inspect::dumpTokens) {
            dumpTokens(tokens);
        }

        tree::ParseTree *cst = nullptr;
        //tree::ParseTree *ast = nullptr;
        AST::node_ptr_t ast = nullptr;
        GCT::node_ptr_t gct = nullptr;
        GIR::graph_ptr_t gir = nullptr;
        context_ptr_t ctx = make_shared<Context>();

        if (!buildCST(cst, parser, os, errorFormat)) {
            return selectedCommand == Command::CHECK ? 0 : 2;
        }
        assert(cst != nullptr);
        if (Inspect::dumpCST) {
            auto visitor = CSTDumpVisitor(os);
            visitor.visit(cst);
        }
        if (Format::formatCode) {
            auto formatter = Formatter(tokens.getTokens());
            const string formattedCode = any_cast<string>(formatter.visit(cst));
            os << formattedCode;
            return 0;
        }

        if (!buildAST(ast, cst, os, errorFormat)) {
            return selectedCommand == Command::CHECK ? 0 : 3;
        }
        assert(ast != nullptr);
        if (Inspect::dumpAST && ast) {
            ast->print(os);
        }

        if (!buildGCT(gct, ast, os, errorFormat)) {
            return selectedCommand == Command::CHECK ? 0 : 4;
        }
        assert(gct != nullptr);
        if (Inspect::dumpGCT && gct) {
            gct->print(os);
        }

        if (!buildGIR(gir, gct, ctx, os, errorFormat)) {
            return selectedCommand == Command::CHECK ? 0 : 5;
        }
        assert(gir != nullptr);
        if (Inspect::dumpGIR) {
            GraphVizDumpPass pass(ctx);
            auto res = pass.apply(gir);
            os << any_cast<string>(res);
        }

        if (Run::profile) {
            endTime = chrono::high_resolution_clock::now();
            auto duration = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
            info << "Time used " << duration << " us" << endl;
        }
    }

    return 0;
}
