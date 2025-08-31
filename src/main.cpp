/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and
 * conditions of the MIT license. You may obtain a copy of
 * the MIT license at: [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT
 * WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
 * INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Sep. 01, 2023
 * Updated: Mar. 17, 2025
 * Supported by: National Key Research and Development
 * Program of China
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

#include "common/module/userdef.h"
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
    if (selectedCommand == Command::Check) {
        errorFormat = Check::outputFormat;
    }

    istream *input;

    if (Run::targetFiles.empty() || Run::targetFiles[0] == "") {
        input = &cin;
        targetFile = "stdin"; // for error reporting
    } else {
        targetFile = Run::targetFiles[0];
        auto src = ifstream();
        src.open(targetFile);
        if (!src.is_open()) {
            log_error << "Error opening file " << targetFile << endl;
            return 1;
        }
        input = &src;
    }

    chrono::high_resolution_clock::time_point startTime, endTime;

    while (Run::repeat--) {
        if (Run::profile) {
            startTime = chrono::high_resolution_clock::now();
        }

        diagnostics_ptr_t diagnostics = make_shared<Diagnostics>();
        if (selectedCommand == Command::Run || selectedCommand == Command::Inspect) {
            diagnostics->setLimit(Diagnostic::Severity::Error, 0);
        }

        bool useJsonFormat = (errorFormat == "json");

        try {
            parser_ptr_t parser = std::make_shared<CamelParser>(diagnostics);
            try {
                parser->parse(*input);
            } catch (CamelBaseException &e) {
                if (selectedCommand == Command::Check) {
                    parser->dumpDiagnostics(os, useJsonFormat);
                    return 0;
                } else {
                    os << e.what(useJsonFormat) << endl;
                    return 1;
                }
            }

            if (selectedCommand == Command::Format) {
                auto formatter = Formatter(parser->getTokens());
                const string formattedCode = any_cast<string>(formatter.visit(parser->cst()));
                os << formattedCode;
                return 0;
            }

            if (selectedCommand == Command::Inspect) {
                if (Inspect::dumpTokens) {
                    parser->dumpTokens(os);
                }
                if (Inspect::dumpCST) {
                    auto cst = parser->cst();
                    auto visitor = CSTDumpVisitor(os);
                    visitor.visit(cst);
                }
                if (!Inspect::dumpAST) {
                    auto ast = parser->ast();
                    if (ast) {
                        ast->print(os);
                    }
                }
                if (!Inspect::dumpGCT && !Inspect::dumpGIR) {
                    // Inspect Command ends here if only
                    // tokens, CST or AST is requested
                    return 0;
                }
            }

            context_ptr_t ctx = std::make_shared<Context>(
                EntryConfig{
                    .root = ".",
                    .searchPaths = {"", "lib"},
                    .entryFile = targetFile.envs =
                        {{"CAMEL_STD_LIB", Run::stdLibPath.empty() ? "lib" : Run::stdLibPath}}},
                DiagnosticsConfig{
                    .total_limit = -1,
                    .per_severity_limits = {{Diagnostic::Severity::Error, 0}}});

            auto mainModule = make_shared<UserDefinedModule>("main", targetFile, ctx, parser);
            ctx->setMainModule(mainModule);

            try {
                mainModule->compile();
            } catch (DiagnosticsLimitExceededException &e) {
                if (selectedCommand == Command::Check) {
                    os << e.lastDiagnostic().what(useJsonFormat) << endl;
                    return 0;
                } else {
                    os << e.lastDiagnostic().what(useJsonFormat) << endl;
                    return 1;
                }
            }

            if (selectedCommand == Command::Inspect) {
                if (Inspect::dumpGCT && mainModule->gct()) {
                    mainModule->gct()->print(os);
                }
                if (Inspect::dumpGIR && mainModule->gir()) {
                    GraphVizDumpPass pass(ctx);
                    auto gir = mainModule->gir();
                    auto res = pass.apply(gir);
                    os << any_cast<string>(res);
                }
                return 0;
            }

            if (!mainModule->ready()) {
                if (selectedCommand == Command::Check) {
                    mainModule->diagnostics()->dump(os, useJsonFormat);
                    return 0;
                } else {
                    mainModule->diagnostics()->dump(os, useJsonFormat);
                    return 1;
                }
            }

        } catch (exception &e) {
            os << e.what() << endl;
            return 1;
        }

        if (Run::profile) {
            endTime = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
            log_info << "Time used " << duration << " us" << endl;
        }
    }

    return 0;
}
