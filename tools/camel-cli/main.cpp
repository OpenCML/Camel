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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "nlohmann/json.hpp"

#include "antlr4-runtime/antlr4-runtime.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/listener.h"
#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/core/type.h"
#include "camel/parse/antlr/OpenCMLLexer.h"
#include "camel/parse/antlr/OpenCMLParser.h"
#include "camel/parse/ast/builder.h"
#include "camel/parse/cst_dumper.h"
#include "camel/parse/parse.h"
#include "camel/utils/dll_path.h"
#include "camel/utils/env.h"
#include "camel/utils/log.h"
#include "camel/utils/memperf.h"
#include "config.h"
#include "passes/trans/dot/graphviz.h"
#include "passes/trans/tns/topo_node_seq.h"
#include "service/codegen/source/generator.h"
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <queue>
#include <string>

namespace mm = camel::core::mm;
using namespace antlr4;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace std;
using namespace camel::core::error;
using namespace camel::parse;

namespace fs = std::filesystem;

using namespace CLI;

#define DEBUG_LEVEL -1

string targetFile = "";

int main(int argc, char *argv[]) {
    camel::utils::setupLibrarySearchPath();
    if (!parseArgs(argc, argv))
        return 0;

    ostream &os = cout;

    string errorFormat = Run::errorFormat;
    if (selectedCommand == Command::Check) {
        errorFormat = Check::outputFormat;
    }

    std::unique_ptr<istream> input;

    if (Run::resolvedInputPath.empty()) {
        input      = std::make_unique<istream>(std::cin.rdbuf());
        targetFile = "stdin"; // for error reporting
        EXEC_WHEN_DEBUG(GetDefaultLogger().in("Main").info("Reading from standard input."));
    } else {
        targetFile = Run::resolvedInputPath;
        auto file  = std::make_unique<std::ifstream>(targetFile);
        if (!file->is_open()) {
            std::cerr << "Error: Cannot open file " << targetFile << endl;
            return 1;
        }
        input = std::move(file);
        EXEC_WHEN_DEBUG(GetDefaultLogger().in("Main").info("Reading from file '{}'.", targetFile));
    }
    diagnostics_ptr_t diagnostics = make_shared<Diagnostics>("main", targetFile);
    if (selectedCommand == Command::Run || selectedCommand == Command::Inspect) {
        diagnostics->setConfig(
            DiagsConfig{
                .total_limit         = -1,
                .per_severity_limits = {{Severity::Error, 0}},
            });
    }

    bool useJsonFormat = (errorFormat == "json");

    fs::path camelPath = camel::utils::getExecutableDirectory();
    if (camelPath.empty())
        camelPath = fs::current_path();
    fs::path entryPath(targetFile);
    // if targetFile is relative (or "stdin"), the entryDir is the current working directory
    // if targetFile is absolute, the entryDir is the parent directory of targetFile
    std::string entryDir = fs::absolute(entryPath).parent_path().string();

    auto addIfNonEmpty = [](std::vector<std::string> &v, const std::string &s) {
        if (!s.empty())
            v.push_back(fs::absolute(fs::path(s)).string());
    };
    std::vector<std::string> searchPaths;

    searchPaths.push_back(entryDir);
    addIfNonEmpty(searchPaths, getEnv("CAMEL_PACKAGES"));
    addIfNonEmpty(searchPaths, Run::stdLibPath.empty() ? getEnv("CAMEL_STD_LIB") : Run::stdLibPath);
    addIfNonEmpty(searchPaths, camelPath.string());
    addIfNonEmpty(searchPaths, (camelPath / "stdlib").string());
    addIfNonEmpty(searchPaths, (camelPath.parent_path() / "stdlib").string());

    context_ptr_t ctx = Context::create(
        EntryConfig{
            .entryDir    = entryDir,
            .entryFile   = targetFile,
            .searchPaths = std::move(searchPaths),
        },
        DiagsConfig{
            .total_limit         = -1,
            .per_severity_limits = {{Severity::Error, 0}},
        });

    parser_ptr_t parser = std::make_shared<CamelParser>(diagnostics);
    auto mainModule     = make_shared<UserDefinedModule>("main", targetFile, ctx, parser);
    ctx->setMainModule(mainModule);

    // Initialize memory management subsystems
    (void)mm::autoSpace();
    (void)mm::metaSpace();
    (void)mm::permSpace();

    while (Run::repeat--) {
        try {
            parser->parse(*input);

            if (selectedCommand == Command::Inspect) {
                if (Inspect::dumpTokens) {
                    parser->dumpTokens(os);
                }
                if (Inspect::dumpCST) {
                    auto cst     = parser->cst();
                    auto visitor = CSTDumpVisitor(os);
                    visitor.visit(cst);
                }
                if (Inspect::dumpAST) {
                    auto ast = parser->ast();
                    if (ast) {
                        ast->print(os);
                    }
                }
                if (Inspect::geneCode) {
                    auto ast = parser->ast();
                    if (ast) {
                        ASTCodeGen::Generator generator = ASTCodeGen::Generator();
                        os << generator.generate(ast);
                    }
                }
                if (!Inspect::dumpGCT && !Inspect::dumpGIR && !Inspect::dumpTNS) {
                    // Inspect Command ends here if only
                    // tokens, CST or AST is requested
                    return 0;
                }
            }

            if (selectedCommand == Command::Inspect) {
                if (Inspect::dumpGCT || Inspect::dumpGIR || Inspect::dumpTNS) {
                    mainModule->compile(CompileStage::GCT);
                }
                if (Inspect::dumpGIR || Inspect::dumpTNS) {
                    mainModule->compile(CompileStage::Done);
                }
            } else {
                mainModule->compile(CompileStage::Done);
            }

            if (selectedCommand == Command::Inspect) {
                if (Inspect::dumpGCT && mainModule->gct()) {
                    mainModule->gct()->print(os);
                }
                if (Inspect::dumpGIR && ctx->rootGraph()) {
                    GraphVizDumpPass pass(ctx);
                    auto root = ctx->rootGraph();
                    auto res  = pass.apply(root, os);
                }
                if (Inspect::dumpTNS && ctx->rootGraph()) {
                    auto entry = ctx->rootGraph();
                    TopoNodeSeqDumpPass pass(ctx);
                    auto res = pass.apply(entry, os);
                }
                return 0;
            }

            if (!mainModule->loaded()) {
                ctx->dumpAllModuleDiagnostics(os, useJsonFormat);
                return selectedCommand == Command::Check ? 0 : 1;
            }

            if (selectedCommand == Command::Run) {
                EXEC_WHEN_DEBUG({
                    if (Run::profile) {
                        // Initialize and start advanced tracing using profiler configuration
                        profiler::AdvancedTracer::Config config;
                        config.enablePerfettoIntegration = true;
                        config.perfettoOutput = "profile_reports/camel_trace.perfetto-trace";
                        config.outputFile     = "profile_reports/camel_trace.json";
                        profiler::start_advanced_tracing(config);
                    }
                });

                // memperf::enable_logging(true);

                memperf::start();

                try {
                    try {
                        int retCode = applyPasses(Run::resolvedPassList, ctx, os);
                        if (retCode != 0) {
                            const auto &diags = ctx->runtimeDiagSink();
                            if (diags->hasErrors()) {
                                diags->dump(os, useJsonFormat);
                            }
                            return retCode;
                        }
                    } catch (Diagnostic &d) {
                        if (!d.persisted) {
                            ctx->runtimeDiagSink()->add(std::move(d));
                        }
                    }
                } catch (DiagnosticsLimitExceededBaseException &e) {
                    const auto &diags = ctx->runtimeDiagSink();
                    diags->dump(os, useJsonFormat);
                    return selectedCommand == Command::Check ? 0 : 1;
                }

                memperf::stop();

                memperf::report(os);

                EXEC_WHEN_DEBUG({
                    if (Run::profile) {
                        profiler::stop_advanced_tracing();
                        profiler::generate_advanced_report();
                    }
                });
            }

        } catch (DiagnosticsLimitExceededBaseException &e) {
            ctx->dumpAllModuleDiagnostics(os, useJsonFormat);
            return selectedCommand == Command::Check ? 0 : 1;
        } catch (Diagnostic &d) {
            RangeConverter conv(parser->getTokens());
            d.fetchRange(conv);
            os << "Uncaught diagnostic: " << (useJsonFormat ? d.toJson() : d.toText()) << endl;
            return selectedCommand == Command::Check ? 0 : 1;
        } catch (exception &e) {
            os << e.what() << endl;
            ASSERT(false, e.what());
            return selectedCommand == Command::Check ? 0 : 1;
        } catch (...) {
            os << "Unknown error occurred." << endl;
            ASSERT(false, "Unknown error occurred.");
            return 1;
        }
    }

    return 0;
}
