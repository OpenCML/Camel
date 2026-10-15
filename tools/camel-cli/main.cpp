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
 * Updated: Oct. 13, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/module/introspect.h"
#include "camel/core/module/userdef.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/type.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/pass/base.h"
#include "camel/execute/pass/snapshot.h"
#include "camel/execute/pass/warnings.h"
#include "camel/init.h"
#include "camel/parse/ast/builder.h"
#include "camel/parse/parse.h"
#include "camel/runtime/operator_instance.h"
#include "camel/utils/install_layout.h"
#include "camel/utils/log.h"
#include "camel/utils/memperf.h"
#include "camel/utils/phase_timer.h"
#include "check.h"
#include "config.h"
#include "passes/trans/dot/graphviz.h"
#include "passes/trans/stats/stats.h"
#include "passes/trans/tns/topo_node_seq.h"
#include "service/codegen/source/generator.h"
#include "service/profiler/advanced/advanced_tracer.h"
#include "service/profiler/core/trace.h"
#include "timeit.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace std;
using namespace camel::core::error;
using namespace camel::parse;

namespace fs = std::filesystem;

using namespace CLI;

#define DEBUG_LEVEL -1

string targetFile = "";

namespace {

/// State of `--dump-after-each-pass <dir>`: one readable DOT (std::rgir format, with the
/// origin/span/srcfile attributes of each node) and one GRAPH_STATS JSON per snapshot.
/// Snapshot 00 is the graph before the first pass; snapshot N is the output of the N-th pass
/// that returned a graph. Passes that consume the graph (scheduling and translation passes)
/// leave nothing to dump.
struct PassDumpState {
    fs::path dir;
    context_ptr_t ctx;
    size_t next = 0;
};

string passDumpStem(size_t index, const string &passName) {
    string safe;
    for (size_t i = 0; i < passName.size(); ++i) {
        const char c = passName[i];
        if (c == ':' && i + 1 < passName.size() && passName[i + 1] == ':') {
            safe += '.';
            ++i;
        } else if (isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-') {
            safe += c;
        } else {
            safe += '_';
        }
    }
    return std::format("{:02}_{}", index, safe);
}

void writePassDump(PassDumpState &state, const string &passName, camel::runtime::GCGraph *graph) {
    const string stem = passDumpStem(state.next++, passName);
    {
        std::ofstream dot(state.dir / (stem + ".rgir.dot"), std::ios::binary);
        dot << dumpReadableRuntimeGraph(graph, state.ctx);
    }
    {
        std::ofstream stats(state.dir / (stem + ".stats.json"), std::ios::binary);
        stats << collectGraphStats(state.ctx, graph).toJson() << '\n';
    }
}

void passDumpHandler(const char *passName, size_t, camel::runtime::GCGraph *graph, void *userData) {
    writePassDump(*static_cast<PassDumpState *>(userData), passName ? passName : "", graph);
}

} // namespace

namespace {

/// Whether the function `main` declared at the top level of the entry module writes a return
/// type (`func main(): int`). Only then is what it returns the result of the program (design
/// principles 2.10). nullopt when the tree holds no such declaration (`main` bound some other
/// way): the value then counts as it always did.
std::optional<bool> entryDeclaresReturnType(const AST::node_ptr_t &node, int depth = 0) {
    if (!node || depth > 3) {
        return std::nullopt;
    }
    if (auto decl = std::dynamic_pointer_cast<AST::FuncDeclLoad>(node->load());
        decl && decl->ref().plain() && decl->ref().ident() == "main") {
        for (const auto &child : *node) {
            if (!child || !std::dynamic_pointer_cast<AST::FuncDataLoad>(child->load())) {
                continue;
            }
            // FuncData(FuncType(with params, params, optional return type), body)
            if (child->size() == 0 || child->at(0)->size() < 3) {
                return std::nullopt;
            }
            return !child->at(0)->at(2)->empty();
        }
        return std::nullopt;
    }
    for (const auto &child : *node) {
        if (auto found = entryDeclaresReturnType(child, depth + 1)) {
            return found;
        }
    }
    return std::nullopt;
}

} // namespace

namespace phase = camel::utils::phase;

/// The whole of a `camel` command. main() is only the frame around it that the phase report
/// (camel/utils/phase_timer.h) needs: the report is written when everything this function
/// owns has been destroyed, so that the teardown of a run is a phase like the others.
int runCommand(int argc, char *argv[]) {
    std::optional<phase::Scope> startupPhase(std::in_place, "startup");
    camel::ScopedRuntime camelRuntime;
    if (!parseArgs(argc, argv))
        return exitCode;

    ostream &os = cout;

    const string errorFormat = Diag::format;

    std::unique_ptr<istream> input;

    if (Run::resolvedInputPath.empty()) {
        input      = std::make_unique<istream>(std::cin.rdbuf());
        targetFile = "stdin"; // for error reporting
        CAMEL_LOG_INFO_S("Main", "Reading from standard input.");
    } else {
        targetFile = Run::resolvedInputPath;
        auto file  = std::make_unique<std::ifstream>(targetFile);
        if (!file->is_open()) {
            std::cerr << "Error: Cannot open file " << targetFile << endl;
            return 1;
        }
        input = std::move(file);
        CAMEL_LOG_INFO_S("Main", "Reading from file '{}'.", targetFile);
    }
    // Compile errors are collected up to --max-errors, so one run reports all it can find. A
    // limit of N lets N errors through: the container gives up when one more than its limit
    // has been stored, hence N - 1.
    const DiagsConfig compileDiagsConfig =
        Diag::maxErrors > 0
            ? DiagsConfig{
                  .total_limit         = -1,
                  .per_severity_limits = {{Severity::Error, Diag::maxErrors - 1}},
              }
            : DiagsConfig{.total_limit = -1};
    // A run-time fault ends the run: the first one stored is the last.
    const DiagsConfig runtimeDiagsConfig{
        .total_limit         = -1,
        .per_severity_limits = {{Severity::Error, 0}},
    };
    diagnostics_ptr_t diagnostics = make_shared<Diagnostics>("main", targetFile);
    diagnostics->setConfig(compileDiagsConfig);

    bool useJsonFormat = (errorFormat == "json");

    if (selectedCommand == Command::Check && (Check::lexical || Check::syntaxOnly)) {
        return checkFrontend(*input, diagnostics, Check::lexical, useJsonFormat, os);
    }

    // Initialize memory management subsystems before any run-mode graph work.
    {
        phase::Scope phaseScope("mm_init");
        (void)mm::autoSpace();
        (void)mm::metaSpace();
        (void)mm::permSpace();
    }

    if (selectedCommand == Command::Run && Run::timeitEnabled) {
        return runTimeitMode(os, useJsonFormat);
    }

    fs::path entryPath(targetFile);
    // if targetFile is relative (or "stdin"), the entryDir is the current working directory
    // if targetFile is absolute, the entryDir is the parent directory of targetFile
    std::string entryDir = fs::absolute(entryPath).parent_path().string();
    auto searchPaths     = camel::utils::buildModuleSearchPaths(
        entryDir,
        camel::utils::ModuleSearchPathOptions{.stdlibOverride = Run::stdLibPath});
    {
        auto installRoot = camel::utils::resolveInstallRoot();
        std::size_t n    = 0;
        for (const auto &p : searchPaths) {
            if (!p.empty())
                ++n;
        }
        const std::string stdlibNote =
            Run::stdLibPath.empty() ? "(CAMEL_STD_LIB unset, use default)" : Run::stdLibPath;
        LogInfoPathList(
            "Main",
            [&] {
                return std::format(
                    "run | module paths | {} entries | CAMEL_HOME={} | entry_dir={} | stdlib={}",
                    n,
                    installRoot.string(),
                    entryDir,
                    stdlibNote);
            },
            searchPaths);
    }

    std::optional<phase::Scope> contextPhase(std::in_place, "context");
    context_ptr_t ctx = Context::create(
        EntryConfig{
            .entryDir          = entryDir,
            .entryFile         = targetFile,
            .searchPaths       = std::move(searchPaths),
            .maxThreads        = Run::maxThreads > 0 ? static_cast<size_t>(Run::maxThreads) : 0,
            .maxRecursionDepth = Run::maxRecursionDepth,
            .maxMacroDepth     = Run::maxMacroDepth,
        },
        compileDiagsConfig);
    // However the run ends (main returns, a fault, exit(n)), it ends by returning from here:
    // the external resources the program still holds are released first, before the context
    // and the modules that own their release code go away (design principles 2.9).
    struct ReleaseExternalResources {
        ~ReleaseExternalResources() { camel::core::rtdata::disposeAllForeignResources(); }
    } releaseExternalResources;
    ctx->runtimeDiagSink()->setConfig(runtimeDiagsConfig);

    parser_ptr_t parser = std::make_shared<CamelParser>(diagnostics);
    auto mainModule     = make_shared<UserDefinedModule>("main", targetFile, ctx, parser);
    ctx->setMainModule(mainModule);
    contextPhase.reset();
    startupPhase.reset();
    // Declared after everything a run owns (the context, the parser, the entry module), so it
    // is destroyed before them, on whichever path the function is left: the phase it opens
    // then covers their destruction, and main() closes it.
    struct TeardownPhase {
        ~TeardownPhase() { phase::open("teardown"); }
    } teardownPhase;

    // Printed after the compile diagnostics in the text formats meant for a person: how many
    // there were, and whether the compiler stopped at the limit.
    auto compileSummary = [&](bool limitReached) {
        if (useJsonFormat || diagnosticFormat() != DiagFormat::Rich) {
            return;
        }
        size_t errors = 0, warnings = 0;
        for (const auto &mod : ctx->allUserModules()) {
            auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
            if (ud && ud->diagnostics() && ud->diagnostics()->hasErrors()) {
                errors += ud->diagnostics()->count(Severity::Error);
                warnings += ud->diagnostics()->count(Severity::Warn);
            }
        }
        if (errors == 0) {
            return;
        }
        os << "\ncamel: " << errors << (errors == 1 ? " error" : " errors");
        if (warnings > 0) {
            os << ", " << warnings << (warnings == 1 ? " warning" : " warnings");
        }
        if (limitReached) {
            os << "; stopped at the limit of " << Diag::maxErrors
               << " errors (--max-errors <n> raises it, 0 removes it)";
        }
        os << endl;
    };

    try {
        std::optional<phase::Scope> parsePhase(std::in_place, "parse", "main");
        const bool parsedOk = parser->parse(*input);
        parsePhase.reset();
        if (!parsedOk) {
            // Syntax errors: nothing after the parser can run, and compiling the module would
            // only read and parse the file a second time.
            ctx->dumpAllModuleDiagnostics(os, useJsonFormat);
            compileSummary(false);
            return 1;
        }
        CAMEL_LOG_INFO_S("Main", "run | parse | done | {}", targetFile);

        if (selectedCommand == Command::Inspect) {
            if (Inspect::dumpTokens) {
                parser->dumpTokens(os);
            }
            if (Inspect::dumpCST) {
                parser->dumpCST(os);
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
            if (!Inspect::dumpGCT && !Inspect::dumpGIR && !Inspect::dumpTNS && !Inspect::dumpOps &&
                !Inspect::dumpInstances) {
                // Inspect Command ends here if only
                // tokens, CST or AST is requested
                return 0;
            }
        }

        if (selectedCommand == Command::Inspect) {
            if (Inspect::dumpGCT || Inspect::dumpGIR || Inspect::dumpTNS) {
                mainModule->compile(CompileStage::GCT);
            }
            if (Inspect::dumpGIR || Inspect::dumpTNS || Inspect::dumpOps || Inspect::dumpInstances) {
                mainModule->compile(CompileStage::Done);
            }
        } else {
            phase::Scope phaseScope("compile", "main");
            mainModule->compile(CompileStage::Done);
        }

        if (selectedCommand == Command::Inspect) {
            if (Inspect::dumpGCT && mainModule->gct()) {
                mainModule->gct()->print(os);
            }
            if (Inspect::dumpGIR) {
                GraphVizDumpPass pass(ctx);
                auto *root = ctx->runtimeRootGraph();
                (void)pass.apply(root, os);
            }
            if (Inspect::dumpTNS) {
                auto *entry = ctx->runtimeRootGraph();
                TopoNodeSeqDumpPass pass(ctx);
                (void)pass.apply(entry, os);
            }
            if (Inspect::dumpInstances) {
                if (auto *root = ctx->runtimeRootGraph()) {
                    camel::runtime::dumpOperatorInstances(root, os);
                }
            }
            if (Inspect::dumpOps) {
                // An import that names nothing the program uses is loaded here, so that the
                // listing does not depend on what the program happens to call.
                auto modules = ctx->importedModules();
                for (const auto &module : modules) {
                    if (!module->loaded()) {
                        module->load();
                    }
                }
                camel::core::module::dumpOperators(modules, os);
            }
            return 0;
        }

        if (!mainModule->loaded()) {
            ctx->dumpAllModuleDiagnostics(os, useJsonFormat);
            compileSummary(false);
            return 1;
        }
        // The program compiled. Its warnings go to stderr, before anything runs and whatever
        // the log level: stdout stays the program's output (and, on an error, the diagnostics
        // array), which is what tools parse. With them, the warning about passes written after
        // a pass that consumes the graph: they can never run.
        std::vector<Diagnostic> commandLineWarnings;
        if (selectedCommand == Command::Run) {
            if (auto neverRun = passesNeverRunWarning(Run::resolvedPassList, ctx)) {
                // Not added to a container, so the warning policy is applied here.
                if (applyWarningPolicy(*neverRun)) {
                    commandLineWarnings.push_back(std::move(*neverRun));
                }
            }
        }
        ctx->dumpModuleWarnings(std::cerr, useJsonFormat, commandLineWarnings);
        for (const Diagnostic &warning : commandLineWarnings) {
            if (warning.severity == Severity::Error) {
                // -Werror made a command-line warning an error: it was reported above with
                // the warnings, and the program is not run.
                return 1;
            }
        }

        CAMEL_LOG_INFO_S(
            "Main",
            "run | compile | runtime_root={} | user_modules={}",
            ctx->runtimeRootGraph() ? ctx->runtimeRootGraph()->name() : std::string{"<none>"},
            ctx->allUserModules().size());

        if (selectedCommand == Command::Run) {
            ctx->clearProcessExitCode();
            EXEC_WHEN_DEBUG({
                if (Run::profile) {
                    // Initialize and start advanced tracing using profiler configuration
                    profiler::AdvancedTracer::Config config;
                    config.enablePerfettoIntegration = true;
                    config.perfettoOutput            = "profile_reports/camel_trace.perfetto-trace";
                    config.outputFile                = "profile_reports/camel_trace.json";
                    profiler::start_advanced_tracing(config);
                }
            });

            // memperf::enable_logging(true);

            memperf::start();

            try {
                try {
                    // All pass names, the fallback's included, are checked before the first
                    // snapshot is written and before any pass runs: an unknown name anywhere
                    // on the command line leaves no effect and no artefact.
                    requireKnownPasses(Run::resolvedPassList, ctx);
                    requireKnownPasses(Run::fallbackPasses, ctx);
                    auto *graph = ctx->runtimeRootGraph();
                    if (phase::enabled()) {
                        // The size of the program as the passes receive it.
                        const GraphStats stats = collectGraphStats(ctx, graph);
                        phase::count("graph.graphs", stats.graphs);
                        phase::count("graph.nodes", stats.nodes);
                        phase::count("graph.edges", stats.edges);
                    }
                    // The values given for the parameters of `main` become constants of the
                    // entry graph before the first pass sees it.
                    graph = camel::execute::bindEntryArgumentSpecs(ctx, graph, Run::entryArgs);
                    if (graph == nullptr) {
                        ctx->runtimeDiagSink()->dump(os, useJsonFormat);
                        return 1;
                    }
                    // What `main` returns goes to the output: the file given with -o, else
                    // standard output (where an exit code is not printed).
                    // Only a `main` that declares its return type has a result.
                    ctx->setEntryResultDeclared(
                        entryDeclaresReturnType(parser->ast()).value_or(true));
                    camel::execute::writeEntryResultTo(
                        ctx,
                        Run::outputFile.empty() || Run::outputFile == "stdout" ? std::string()
                                                                               : Run::outputFile,
                        useJsonFormat);
                    static PassDumpState passDump;
                    if (!Run::dumpAfterEachPassDir.empty()) {
                        passDump.dir = Run::dumpAfterEachPassDir;
                        passDump.ctx = ctx;
                        std::error_code ec;
                        fs::create_directories(passDump.dir, ec);
                        if (ec) {
                            std::cerr << "Error: cannot create snapshot directory "
                                      << passDump.dir.string() << ": " << ec.message() << endl;
                            return 1;
                        }
                        writePassDump(passDump, "input", graph);
                        setPassSnapshotHandler(&passDumpHandler, &passDump);
                    }
                    // A pass's warnings about the program go where the compiler's go: to
                    // stderr, in the format of the diagnostics, when the pass has run.
                    static bool passWarningsAsJson = false;
                    passWarningsAsJson             = useJsonFormat;
                    setPassWarningHandler(
                        [](const char *, const std::vector<Diagnostic> &raised, void *) {
                            std::vector<const Diagnostic *> warnings;
                            for (const Diagnostic &warning : raised) {
                                if (admitShownWarning()) {
                                    warnings.push_back(&warning);
                                }
                            }
                            if (warnings.empty()) {
                                return;
                            }
                            const bool rich = diagnosticFormat() == DiagFormat::Rich;
                            if (passWarningsAsJson) {
                                std::cerr << "[\n";
                            }
                            for (size_t i = 0; i < warnings.size(); ++i) {
                                const bool last = i + 1 == warnings.size();
                                std::cerr << (passWarningsAsJson ? warnings[i]->toJson()
                                                                 : warnings[i]->toText(std::cerr))
                                          << (passWarningsAsJson ? (last ? "\n" : ",\n")
                                                                 : (rich ? "\n\n" : "\n"));
                            }
                            if (passWarningsAsJson) {
                                std::cerr << "]\n";
                            }
                            std::cerr << std::flush;
                        },
                        nullptr);
                    auto result = applyPassesDetailed(graph, Run::resolvedPassList, ctx, os);
                    graph       = result.graph;
                    if (result.failed()) {
                        const auto &diags = ctx->runtimeDiagSink();
                        if (diags->hasErrors()) {
                            diags->dump(os, useJsonFormat);
                            return 1;
                        }
                        os << "[camel] execution failed without diagnostics after resolved passes."
                           << endl;
                        return 1;
                    }
                    if (!result.consumed()) {
                        auto fallbackResult =
                            applyPassesDetailed(graph, Run::fallbackPasses, ctx, os);
                        graph = fallbackResult.graph;
                        if (fallbackResult.failed()) {
                            const auto &diags = ctx->runtimeDiagSink();
                            if (diags->hasErrors()) {
                                diags->dump(os, useJsonFormat);
                                return 1;
                            }
                            os << "[camel] execution failed without diagnostics after fallback "
                                  "passes."
                               << endl;
                            return 1;
                        }
                    }
                } catch (Diagnostic &d) {
                    if (!d.persisted) {
                        ctx->runtimeDiagSink()->add(std::move(d));
                    }
                }
            } catch (DiagnosticsLimitExceededBaseException &e) {
                const auto &diags = ctx->runtimeDiagSink();
                diags->dump(os, useJsonFormat);
                return 1;
            }

            memperf::stop();

            memperf::report(os);

            if (!useJsonFormat && warningsNotShown() > 0) {
                std::cerr << "camel: " << warningsNotShown()
                          << " more warning(s) not shown (--max-warning " << Diag::maxWarnings
                          << ")" << std::endl;
            }

            EXEC_WHEN_DEBUG({
                if (Run::profile) {
                    profiler::stop_advanced_tracing();
                    profiler::generate_advanced_report();
                }
            });

            return ctx->processExitCodeOr(0);
        }
    } catch (DiagnosticsLimitExceededBaseException &e) {
        ctx->dumpAllModuleDiagnostics(os, useJsonFormat);
        compileSummary(true);
        return 1;
    } catch (Diagnostic &d) {
        RangeConverter conv(parser->tokenPositions());
        d.fetchRange(conv);
        os << "Uncaught diagnostic: " << (useJsonFormat ? d.toJson() : d.toText(os)) << endl;
        return 1;
    } catch (const mm::MemoryExhausted &e) {
        // A memory space reached its limit outside any pass (while the program was compiled
        // into graphs). The pass driver and the graph builder report theirs; this is the rest.
        // It is a limit the program ran into, told as a diagnostic with the space, the usage,
        // the limit and the setting that raises it.
        setPipelineStage("compile");
        try {
            auto diag = ctx->runtimeDiagSink()->of(RuntimeDiag::MemoryExhausted);
            if (e.origin() != camel::source::kInvalidOriginId) {
                diag.atOrigin(e.origin());
            }
            diag.commit(std::string(), std::string(e.what()));
        } catch (const DiagnosticsLimitExceededBaseException &) {
            // The sink stops at its first error; the diagnostic is recorded and dumped below.
        }
        setPipelineStage("");
        ctx->runtimeDiagSink()->dump(os, useJsonFormat);
        return 1;
    } catch (exception &e) {
        // An exception that is not a diagnostic and was not raised inside a pass (the pass
        // driver reports those itself). JSON output stays a diagnostics array.
        if (useJsonFormat) {
            os << "[\n"
               << DiagnosticBuilder::of(InternalDiag::UnknownInternalError)
                      .commit(std::string(e.what()))
                      .toJson()
               << "\n]" << endl;
        } else {
            // Not a diagnostic: a defect of the compiler. Said as such, with the text of the
            // exception (it used to be printed bare: a line reading `bad any_cast`).
            os << "camel: internal error: " << e.what()
               << "\nThis is a defect of the compiler, not an error of the program. Please "
                  "report it with the program that shows it."
               << endl;
        }
        ASSERT(false, e.what());
        return 1;
    } catch (...) {
        os << "Unknown error occurred." << endl;
        ASSERT(false, "Unknown error occurred.");
        return 1;
    }

    return 0;
}

int main(int argc, char *argv[]) {
    // Before the first call into libcamel: on Windows the executable loads libcamel.dll lazily
    // (/DELAYLOAD), and initialize() is what adds the directories it is found in. A call made
    // earlier ends the process with "module not found" (0xC06D007E) before any output.
    // initialize() is idempotent; runCommand's ScopedRuntime calls it again.
    camel::initialize();
    phase::enableFromEnvironment();
    // `--time-phases` (report to standard error) and `--time-phases=<file>` are taken out of
    // argv here, before the argument parser runs, so that the report covers the start of the
    // run as well. The file is joined to the option: a separate value would take the program
    // that follows for the file, and the report would be written over the program.
    std::vector<char *> args;
    for (int i = 0; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (i > 0 && arg == "--time-phases") {
            phase::enable("-");
        } else if (i > 0 && arg.starts_with("--time-phases=")) {
            phase::enable(std::string(arg.substr(std::string_view("--time-phases=").size())));
        } else {
            args.push_back(argv[i]);
        }
    }
    args.push_back(nullptr);
    const int code = runCommand(static_cast<int>(args.size()) - 1, args.data());
    phase::closeOpen();
    phase::report();
    return code;
}
