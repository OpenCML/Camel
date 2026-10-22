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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/mm.h"
#include "camel/core/module/introspect.h"
#include "camel/core/module/userdef.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/tags.h"
#include "camel/core/type.h"
#include "camel/execute/entry_args.h"
#include "camel/execute/pass/base.h"
#include "camel/execute/pass/params.h"
#include "camel/execute/pass/snapshot.h"
#include "camel/execute/pass/warnings.h"
#include "camel/init.h"
#include "camel/parse/ast/builder.h"
#include "camel/parse/parse.h"
#include "camel/profile/profile.h"
#include "camel/runtime/facts.h"
#include "camel/runtime/operator_instance.h"
#include "camel/utils/command_line.h"
#include "camel/utils/install_layout.h"
#include "camel/utils/log.h"
#include "camel/utils/memperf.h"
#include "camel/utils/phase_timer.h"
#include "check.h"
#include "config.h"
#include "module_failures.h"
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

/// State of `--dump-after-each-pass <dir>`: one readable DOT (std::gir format, with the
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
        std::ofstream dot(state.dir / (stem + ".gir.dot"), std::ios::binary);
        dot << dumpCompactRuntimeGraph(graph, state.ctx);
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
/// principles 9.8). nullopt when the tree holds no such declaration (`main` bound some other
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
        auto file  = std::make_unique<std::ifstream>(camel::utils::pathFromUtf8(targetFile));
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

    fs::path entryPath = camel::utils::pathFromUtf8(targetFile);
    // if targetFile is relative (or "stdin"), the entryDir is the current working directory
    // if targetFile is absolute, the entryDir is the parent directory of targetFile
    std::string entryDir = camel::utils::pathToUtf8(fs::absolute(entryPath).parent_path());
    auto searchPaths     = camel::utils::buildModuleSearchPaths(
        entryDir,
        camel::utils::ModuleSearchPathOptions{
            .stdlibOverride = Run::stdLibPath, .includeDirs = Run::includeDirs});
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
                    camel::utils::pathToUtf8(installRoot),
                    entryDir,
                    stdlibNote);
            },
            searchPaths);
    }

    std::optional<phase::Scope> contextPhase(std::in_place, "context");
    // The entry module must define `main` only when the command executes the program.
    // `inspect` and `check` analyse a module (a library included) and never enter it; passes
    // that translate the graph (std::gir, std::jgir, ...) do the same. Requiring `main` there
    // would reject a valid library before its graphs can be dumped or checked. Deciding this
    // for the pass list needs the context, because a pass may be contributed by a module.
    const bool analyseOnly =
        selectedCommand == Command::Inspect || selectedCommand == Command::Check;
    context_ptr_t ctx = Context::create(
        EntryConfig{
            .entryDir          = entryDir,
            .entryFile         = targetFile,
            .searchPaths       = std::move(searchPaths),
            .maxThreads        = Run::maxThreads > 0 ? static_cast<size_t>(Run::maxThreads) : 0,
            .maxRecursionDepth = Run::maxRecursionDepth,
            .maxMacroDepth     = Run::maxMacroDepth,
            .requireEntry      = !analyseOnly,
            .maxMacroRounds    = Run::maxMacroRounds,
        },
        compileDiagsConfig);
    // However the run ends (main returns, a fault, exit(n)), it ends by returning from here:
    // the external resources the program still holds are released first, before the context
    // and the modules that own their release code go away (design principles 3.9).
    struct ReleaseExternalResources {
        ~ReleaseExternalResources() { camel::core::rtdata::disposeAllForeignResources(); }
    } releaseExternalResources;
    ctx->runtimeDiagSink()->setConfig(runtimeDiagsConfig);

    parser_ptr_t parser = std::make_shared<CamelParser>(diagnostics);
    auto mainModule     = make_shared<UserDefinedModule>("main", targetFile, ctx, parser);
    ctx->setMainModule(mainModule);
    // The pass list is resolved against a context that has the program's module. Doing it
    // before the build leaves the entry requirement decided before anything is compiled.
    if (selectedCommand == Command::Run && passListTranslatesProgram(Run::resolvedPassList, ctx)) {
        ctx->setRequireEntry(false);
    }
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
        // The same scope the diagnostic report used: a pass library the pass list loaded is not
        // part of the program, so its errors are not counted in the program's summary either
        // (H198/H210). `compileSummary` is only called on paths that asked ProgramDependencies.
        for (const auto &mod :
             camel_cli::findModuleFailures(ctx, camel_cli::ModuleScope::ProgramDependencies)
                 .modules) {
            auto ud = std::dynamic_pointer_cast<UserDefinedModule>(mod);
            if (ud && ud->diagnostics()) {
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
        // `std::profile` reports the phase split of the compilation, which starts before any pass
        // runs: the recorder has to be recording before the first module is parsed. The report
        // is taken by the pass's report at the end of the run, so no phase file is written.
        if (selectedCommand == Command::Run && pipelineNamesProfile(Run::resolvedPassList)) {
            phase::enableRecording();
        }
        std::optional<phase::Scope> parsePhase(std::in_place, "parse", "main");
        const bool parsedOk = parser->parse(*input);
        parsePhase.reset();
        if (!parsedOk) {
            // Syntax errors: nothing after the parser can run, and compiling the module would
            // only read and parse the file a second time.
            //
            // The program's dependencies, not every module in the context: resolving the pass
            // list above imported the pass's library, and that library is not part of the
            // program the user asked to run. Its own parse error is reported by pass resolution
            // (as `InvalidPassParameter`) when the program gets that far; printing it here, as
            // a bare `SyntaxDiag`, would answer a question the user did not ask (H198/H210).
            camel_cli::reportModuleFailures(
                ctx,
                camel_cli::findModuleFailures(
                    ctx, camel_cli::ModuleScope::ProgramDependencies),
                os,
                useJsonFormat);
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
                !Inspect::dumpInstances && !Inspect::dumpFacts && !Inspect::dumpTags) {
                // Inspect Command ends here if only
                // tokens, CST or AST is requested
                return 0;
            }
        }

        if (selectedCommand == Command::Inspect) {
            if (Inspect::dumpGCT || Inspect::dumpGIR || Inspect::dumpTNS) {
                mainModule->compile(CompileStage::GCT);
            }
            if (Inspect::dumpGIR || Inspect::dumpTNS || Inspect::dumpOps ||
                Inspect::dumpInstances || Inspect::dumpFacts || Inspect::dumpTags) {
                mainModule->compile(CompileStage::Done);
            }
            // Every dump below reads something the compile produced, so the question is asked
            // once, before the first one: did anything fail? A `--gct` dump used to print the
            // construction tree even when the tree was partial and then exit 0 (H181), which is
            // H172's `--ops` silence on a different output path. `--ops` keeps the contract that
            // H172 gave it: the listing is what stdout carries, so the diagnostics and the
            // declaration files that failed go to stderr.
            //
            // The scope is the program's dependencies: these dumps are of the program `inspect`
            // compiled, and the failure that makes a dump untrue is one in its entry module or
            // in a module it imports. Nothing else has been loaded on this path.
            const bool listing = Inspect::dumpOps;
            if (camel_cli::failIfModuleErrors(
                    ctx,
                    camel_cli::ModuleScope::ProgramDependencies,
                    listing ? std::cerr : os,
                    useJsonFormat,
                    listing ? &std::cerr : nullptr)) {
                if (!listing) {
                    compileSummary(false);
                }
                return 1;
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
                (void)pass.apply(ctx->runtimeProgram(), os);
            }
            if (Inspect::dumpTNS) {
                TopoNodeSeqDumpPass pass(ctx);
                (void)pass.apply(ctx->runtimeProgram(), os);
            }
            if (Inspect::dumpInstances) {
                if (ctx->runtimeProgram()->root()) {
                    camel::runtime::dumpOperatorInstances(*ctx->runtimeProgram(), os);
                }
            }
            if (Inspect::dumpTags) {
                // What the program declares as tags, and the facts written on its declarations
                // and fields (design principles 6.2). The `on nodes` facts are node attributes,
                // shown by the graph dumps (`attrs=`).
                auto modules = ctx->allUserModules();
                for (const auto &module : modules) {
                    camel::core::tag::dump(*module, os);
                }
            }
            if (Inspect::dumpFacts) {
                if (ctx->runtimeProgram()->root()) {
                    camel::runtime::fact::FactStore &facts = ctx->facts();
                    camel::runtime::fact::computeFacts(facts, *ctx->runtimeProgram());
                    camel::runtime::fact::dumpFacts(facts, *ctx->runtimeProgram(), os);
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
                // A listing is only worth printing when nothing it should contain is missing. A
                // module that failed to compile leaves its operators out, and this tool is how
                // the inventory is checked: exiting 0 with a shorter list is the silence that
                // hid the `pyplot`/`python` registration gap (handover H166, H172). Loading a
                // module here can still fail (a module nothing compiled yet), so the same
                // question the compile asked is asked again, after the load, by the same helper.
                // A module that is not imported is not looked at, and a C++ module whose
                // signatures are computed (no `.cml` beside its `.cmo`) has no declaration file
                // to fail.
                //
                // The scope is every user module: the listing is the inventory of every module
                // the context holds, including the imported ones just loaded above (H172). A
                // module that failed to compile leaves its operators out wherever it sits, so
                // this is the one command whose question is about all of them.
                if (camel_cli::failIfModuleErrors(
                        ctx,
                        camel_cli::ModuleScope::AllUserModules,
                        std::cerr,
                        useJsonFormat,
                        &std::cerr)) {
                    return 1;
                }
                camel::core::module::dumpOperators(modules, os);
            }
            return 0;
        }

        // The program's dependencies, not every module in the context: a run is about the entry
        // module and what it imports. Resolving the pass list above imported the pass's library
        // (and compiling a pass written in Camel imported what that library imports), so a pass
        // library that does not parse has its diagnostics in the context by now — but it is not
        // a dependency of the program, and stopping here would replace the pass resolution's
        // `InvalidPassParameter` (with the parse error as its secondary location) with the raw
        // parse diagnostic (H198). A pass that is really unusable is reported by the pipeline,
        // which runs next and has the pass's name to put in the message.
        if (camel_cli::failIfModuleErrors(
                ctx,
                camel_cli::ModuleScope::ProgramDependencies,
                os,
                useJsonFormat)) {
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
            // A tag whose declaration names the passes that read it, written into a pipeline
            // that has none of them: nothing reads it, so writing it had no effect.
            for (auto &unread : tagsNeverReadWarnings(Run::resolvedPassList, ctx)) {
                if (applyWarningPolicy(unread)) {
                    commandLineWarnings.push_back(std::move(unread));
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
            ctx->runtimeProgram()->root() ? ctx->runtimeProgram()->root()->name()
                                          : std::string{"<none>"},
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
                    config.openPerfettoUI            = Run::profileModeOpt == "full";
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
                    const auto *program = ctx->runtimeProgram();
                    if (phase::enabled()) {
                        // The size of the program as the passes receive it.
                        const GraphStats stats = collectGraphStats(*program, program->root());
                        phase::count("graph.graphs", stats.graphs);
                        phase::count("graph.nodes", stats.nodes);
                        phase::count("graph.edges", stats.edges);
                    }
                    // The values given for the parameters of `main` become constants of the
                    // entry graph before the first pass sees it.
                    program = camel::execute::bindEntryArgumentSpecs(ctx, program, Run::entryArgs);
                    if (program == nullptr) {
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
                        passDump.dir = camel::utils::pathFromUtf8(Run::dumpAfterEachPassDir);
                        passDump.ctx = ctx;
                        std::error_code ec;
                        fs::create_directories(passDump.dir, ec);
                        if (ec) {
                            std::cerr << "Error: cannot create snapshot directory "
                                      << camel::utils::pathToUtf8(passDump.dir)
                                      << ": " << ec.message() << endl;
                            return 1;
                        }
                        writePassDump(passDump, "input", program->root());
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
                    auto result = applyPassesDetailed(program, Run::resolvedPassList, ctx, os);
                    program     = result.program;
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
                            applyPassesDetailed(program, Run::fallbackPasses, ctx, os);
                        program = fallbackResult.program;
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

            // `std::profile`: the report of the whole run. It is written here because only the
            // driver has seen the compilation, the pipeline and the fallback pass; the session
            // itself was started by the pass. The text form joins the program's output; under
            // `--output-format json` it goes to standard error, so the diagnostics array on the
            // standard output keeps its shape.
            if (camel::profile::enabled() && camel::profile::reportOptions().report) {
                std::ostream &reportOut = useJsonFormat ? std::cerr : os;
                camel::profile::reportText(reportOut, camel::profile::reportOptions());
            }

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
        // The compile stopped at its error limit. Report the program's own modules, the ones
        // `module_failures.h` names; a pass library is not one of them (H198/H210).
        camel_cli::reportModuleFailures(
            ctx,
            camel_cli::findModuleFailures(ctx, camel_cli::ModuleScope::ProgramDependencies),
            os,
            useJsonFormat);
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

#ifdef _WIN32
int wmain(int argc, wchar_t *argv[]) {
#else
int main(int argc, char *argv[]) {
#endif
    // Before the first call into libcamel: on Windows the executable loads libcamel.dll lazily
    // (/DELAYLOAD), and initialize() is what adds the directories it is found in. A call made
    // earlier ends the process with "module not found" (0xC06D007E) before any output.
    // initialize() is idempotent; runCommand's ScopedRuntime calls it again.
    camel::initialize();
    phase::enableFromEnvironment();
    // The command line is UTF-8 from here on: on Windows `wmain` is what receives it as UTF-16,
    // and the narrow `main` form is decoded in the process code page (see command_line.h).
    std::vector<std::string> arguments = camel::utils::utf8Arguments(argc, argv);
    // `--time-phases` (report to standard error) and `--time-phases=<file>` are taken out of
    // argv here, before the argument parser runs, so that the report covers the start of the
    // run as well. The file is joined to the option: a separate value would take the program
    // that follows for the file, and the report would be written over the program.
    //
    // `-I` / `--include` are taken out the same way. clipp's repeatable value parameter keeps
    // consuming the non-option arguments that follow it, so `camel -I root prog.cml` handed
    // `prog.cml` to `-I` as a second root and left the run without an input file. One root per
    // occurrence, in command-line order, is the promised form (docs/cli.md).
    std::vector<char *> args;
    for (size_t i = 0; i < arguments.size(); ++i) {
        std::string &arg = arguments[i];
        if (i > 0 && arg == "--time-phases") {
            phase::enable("-");
        } else if (i > 0 && arg.starts_with("--time-phases=")) {
            phase::enable(std::string(arg.substr(std::string_view("--time-phases=").size())));
        } else if (i > 0 && (arg == "-I" || arg == "--include")) {
            if (i + 1 >= arguments.size()) {
                std::cerr << "Error: " << arg << " requires a directory." << std::endl;
                return 1;
            }
            Run::includeDirs.push_back(arguments[++i]);
        } else if (i > 0 && arg.starts_with("--include=")) {
            std::string dir = arg.substr(std::string_view("--include=").size());
            if (dir.empty()) {
                std::cerr << "Error: --include requires a directory." << std::endl;
                return 1;
            }
            Run::includeDirs.push_back(std::move(dir));
        } else if (i > 0 && arg.size() > 2 && arg.starts_with("-I")) {
            Run::includeDirs.push_back(arg.substr(2));
        } else {
            // The parser only reads an argument; the strings above outlive the call.
            args.push_back(arg.data());
        }
    }
    args.push_back(nullptr);
    const int code = runCommand(static_cast<int>(args.size()) - 1, args.data());
    phase::closeOpen();
    phase::report();
    return code;
}
