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
#include <queue>

#include "nlohmann/json.hpp"

#include "antlr4-runtime/antlr4-runtime.h"
#include "parse/antlr/OpenCMLLexer.h"
#include "parse/antlr/OpenCMLParser.h"

#include "builtin/passes/girdump/graphviz.h"
#include "common/error/base.h"
#include "common/error/diagnostic.h"
#include "common/error/listener.h"
#include "common/type.h"
#include "config.h"
#include "parse/ast.h"
#include "parse/cst-dump.h"
#include "service/formatter/fmt.h"
#include "service/profiler/trace.h"
#include "utils/log.h"

#include "parse/parse.h"

using namespace antlr4;
using namespace std;

using namespace CLI;

#define DEBUG_LEVEL -1

string targetFile = "";

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

        diagnostics_ptr_t diagnostics = make_shared<Diagnostics>();
        if (selectedCommand == Command::RUN | selectedCommand == Command::INSPECT) {
            diagnostics->setLimit(Diagnostic::Severity::Error, 0);
        }

        auto printDiagnostics = [&tokens, &diagnostics, &os, &errorFormat]() {
            const auto &tokenVec = tokens.getTokens();
            while (!diagnostics->end()) {
                auto diagOpt = diagnostics->next();
                if (diagOpt.has_value()) {
                    auto &diag = diagOpt.value();
                    os << diag.fetchRange(tokenVec).what(errorFormat == "json") << std::endl;
                }
            }
        };

        tree::ParseTree *cst = nullptr;
        AST::node_ptr_t ast = nullptr;
        GCT::node_ptr_t gct = nullptr;
        GIR::graph_ptr_t gir = nullptr;
        context_ptr_t ctx = Context::create();

        try {
            if (!buildCST(cst, parser, os, errorFormat)) {
                return selectedCommand == Command::CHECK ? 0 : 2;
            }
            if (Inspect::dumpCST) {
                auto visitor = CSTDumpVisitor(os);
                visitor.visit(cst);
                if (!Inspect::dumpAST && !Inspect::dumpGCT && !Inspect::dumpGIR) {
                    return 0; // If only CST is requested, we can stop here
                }
            }
            if (Format::formatCode) {
                auto formatter = Formatter(tokens.getTokens());
                const string formattedCode = any_cast<string>(formatter.visit(cst));
                os << formattedCode;
                return 0;
            }

            if (!buildAST(ast, cst, diagnostics)) {
                return selectedCommand == Command::CHECK ? 0 : 3;
            }
            if (Inspect::dumpAST && ast) {
                ast->print(os);
                if (!Inspect::dumpGCT && !Inspect::dumpGIR) {
                    return 0; // If only AST is requested, we can stop here
                }
            }

            if (!buildGCT(gct, ast, diagnostics)) {
                return selectedCommand == Command::CHECK ? 0 : 4;
            }
            if (Inspect::dumpGCT && gct) {
                gct->print(os);
                if (!Inspect::dumpGIR) {
                    return 0; // If only GCT is requested, we can stop here
                }
            }

            if (!buildGIR(gir, gct, ctx, diagnostics)) {
                return selectedCommand == Command::CHECK ? 0 : 5;
            }
            if (Inspect::dumpGIR) {
                GraphVizDumpPass pass(ctx);
                auto res = pass.apply(gir);
                os << any_cast<string>(res);
            }

        } catch (DiagnosticsLimitExceededException &e) {
            if (selectedCommand == Command::CHECK) {
                printDiagnostics();
                os << e.lastDiagnostic().fetchRange(tokens.getTokens()).what(errorFormat == "json") << endl;
                return 0;
            } else {
                os << e.lastDiagnostic().fetchRange(tokens.getTokens()).what(errorFormat == "json") << endl;
                return 1;
            }
        } catch (CamelBaseException &e) {
            if (selectedCommand == Command::CHECK) {
                printDiagnostics();
                return 0;
            } else {
                os << e.what(errorFormat == "json") << endl;
                return 1;
            }
        } catch (exception &e) {
            if (errorFormat != "json") {
                os << "An error occurred: " << e.what() << endl;
            } else {
                os << "{"
                   << "\"type\": \"error\", "
                   << "\"filename\": \"" << targetFile << "\", "
                   << "\"line\": 0, "
                   << "\"column\": 0, "
                   << "\"message\": \"An error occurred: " << e.what() << "\""
                   << "}" << endl;
            }
            return selectedCommand == Command::CHECK ? 0 : 1;
        }

        if (Run::profile) {
            endTime = chrono::high_resolution_clock::now();
            auto duration = chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
            info << "Time used " << duration << " us" << endl;
        }
    }

    return 0;
}
