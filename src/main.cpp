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
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <queue>

#include "nlohmann/json.hpp"

#include "antlr4-runtime/antlr4-runtime.h"
#include "common/error/base.h"
#include "common/error/diagnostic.h"
#include "common/error/listener.h"
#include "common/type.h"
#include "config.h"
#include "parse/antlr/OpenCMLLexer.h"
#include "parse/antlr/OpenCMLParser.h"
#include "parse/ast.h"
#include "parse/cst-dump.h"
#include "service/formatter/fmt.h"
#include "service/profiler/trace.h"
#include "utils/env.h"

#include "common/module/userdef.h"
#include "parse/parse.h"

#include "builtin/passes/sched/linear/dump/graphviz.h"
#include "builtin/passes/sched/linear/dump/topo-node-seq.h"
#include "builtin/passes/sched/linear/exec/fallback.h"

#include "utils/log.h"

using namespace antlr4;
using namespace std;

namespace fs = std::filesystem;

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

    std::unique_ptr<istream> input;

    if (Run::targetFiles.empty() || Run::targetFiles[0] == "") {
        input = std::make_unique<istream>(std::cin.rdbuf());
        targetFile = "stdin"; // for error reporting
        l.in("Main").info("Reading from standard input.");
    } else {
        targetFile = Run::targetFiles[0];
        auto file = std::make_unique<std::ifstream>(targetFile);
        if (!file->is_open()) {
            std::cerr << "Error: Cannot open file " << targetFile << endl;
            return 1;
        }
        input = std::move(file);
        l.in("Main").info("Reading from file '{}'.", targetFile);
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
                if (Inspect::dumpAST) {
                    auto ast = parser->ast();
                    if (ast) {
                        ast->print(os);
                    }
                }
                if (!Inspect::dumpGCT && !Inspect::dumpGIR && !Inspect::dumpTNS) {
                    // Inspect Command ends here if only
                    // tokens, CST or AST is requested
                    return 0;
                }
            }

            fs::path camelPath = fs::current_path();
            fs::path entryPath(targetFile);
            // if targetFile is relative (or "stdin"), the entryDir is the current working directory
            // if targetFile is absolute, the entryDir is the parent directory of targetFile
            std::string entryDir = fs::absolute(entryPath).parent_path().string();

            context_ptr_t ctx = std::make_shared<Context>(
                EntryConfig{
                    .entryDir = entryDir,
                    .entryFile = targetFile,
                    .searchPaths =
                        {entryDir,
                         fs::absolute(fs::path(
                                          Run::stdLibPath.empty()
                                              ? getEnv("CAMEL_STD_LIB", "./stdlib")
                                              : Run::stdLibPath))
                             .string(),
                         getEnv("CAMEL_PACKAGES"),
                         getEnv("CAMEL_HOME", camelPath.string())}},
                DiagnosticsConfig{
                    .total_limit = -1,
                    .per_severity_limits = {{Diagnostic::Severity::Error, 0}}});

            auto mainModule = make_shared<UserDefinedModule>("main", targetFile, ctx, parser);
            ctx->setMainModule(mainModule);

            try {
                initTypes();
                mainModule->load();
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
                if (Inspect::dumpGIR && ctx->rootGraph()) {
                    GraphVizDumpPass pass(ctx);
                    auto root = ctx->rootGraph();
                    auto res = pass.apply(root);
                    os << any_cast<string>(res);
                }
                if (Inspect::dumpTNS && ctx->mainGraph()) {
                    auto entry = ctx->mainGraph();
                    TopoNodeSeqDumpPass pass(ctx);
                    auto res = pass.apply(entry);
                    os << any_cast<string>(res);
                }
                return 0;
            }

            if (!mainModule->loaded()) {
                if (selectedCommand == Command::Check) {
                    mainModule->diagnostics()->dump(os, useJsonFormat);
                    return 0;
                } else {
                    mainModule->diagnostics()->dump(os, useJsonFormat);
                    return 1;
                }
            }

            if (selectedCommand == Command::Run) {
                FallbackExecSchedPass pass(ctx);
                pass.apply(ctx->mainGraph());
                return 0;
            }

        } catch (CamelBaseException &e) {
            os << e.what(useJsonFormat) << endl;
            return 1;
        } catch (exception &e) {
            os << e.what() << endl;
            return 1;
        } catch (...) {
            os << "Unknown error occurred." << endl;
            return 1;
        }

        if (Run::profile) {
            endTime = chrono::high_resolution_clock::now();
            auto duration =
                chrono::duration_cast<chrono::microseconds>(endTime - startTime).count();
            l.in("Main").info("Time used: {} us", duration);
        }
    }

    return 0;
}
