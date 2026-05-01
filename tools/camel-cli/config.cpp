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
 * Created: Mar. 17, 2024
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <string_view>
#include <vector>

#include "build_config.h"
#include "camel/utils/log.h"
#include "camel/utils/str.h"
#include "clipp/clipp.h"
#include "config.h"
#include "opencmlrc.h"

#include <filesystem>

#ifndef BUILD_FOOTPRINT
#define BUILD_FOOTPRINT "%y%m%d_%H%M%S"
#endif

using namespace clipp;
using namespace std;
namespace fs = std::filesystem;

namespace {

LogLevel parseLogLevelToken(std::string_view s) {
    std::string t(strutil::trim(s));
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
        return static_cast<char>(::tolower(c));
    });
    if (t == "trace")
        return LogLevel::Trace;
    if (t == "debug")
        return LogLevel::Debug;
    if (t == "info")
        return LogLevel::Info;
    if (t == "warn" || t == "warning")
        return LogLevel::Warn;
    if (t == "fatal")
        return LogLevel::Fatal;
    if (t == "off")
        return LogLevel::Off;
    std::cerr << "Unknown log level: '" << s << "'" << std::endl;
    std::exit(1);
}

LogCategoryPreset parseLogPresetToken(std::string_view s) {
    std::string t(strutil::trim(s));
    std::transform(t.begin(), t.end(), t.begin(), [](unsigned char c) {
        return static_cast<char>(::tolower(c));
    });
    if (t.empty() || t == "none")
        return LogCategoryPreset::None;
    if (t == "wall")
        return LogCategoryPreset::Wall;
    if (t == "extra")
        return LogCategoryPreset::Extra;
    std::cerr << "Unknown --log-preset: '" << s << "'" << std::endl;
    std::exit(1);
}

std::vector<std::string> splitCommaScopes(std::string_view s) {
    std::vector<std::string> out;
    std::string acc;
    for (char c : s) {
        if (c == ',') {
            std::string p(strutil::trim(acc));
            if (!p.empty())
                out.push_back(std::move(p));
            acc.clear();
        } else {
            acc += c;
        }
    }
    std::string p(strutil::trim(acc));
    if (!p.empty())
        out.push_back(std::move(p));
    return out;
}

/// Left-to-right over argv: last `--log-level` / `-v*` wins; then applies preset/include and stderr
/// sink.
void applyCamelCliLogging(int argc, char **argv) {
    static std::atomic<bool> stderrSinkRegistered{false};

    LogLevel lastLevel      = LogLevel::Fatal;
    std::string lastPreset  = "none";
    std::string lastInclude = "";

    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "-l" || a == "--log-level") {
            if (i + 1 < argc)
                lastLevel = parseLogLevelToken(argv[++i]);
            continue;
        }
        if (a == "--log-preset") {
            if (i + 1 < argc)
                lastPreset = argv[++i];
            continue;
        }
        if (a == "--log-include") {
            if (i + 1 < argc)
                lastInclude = argv[++i];
            continue;
        }
        if (a == "-vvvv") {
            lastLevel = LogLevel::Trace;
            continue;
        }
        if (a == "-vvv") {
            lastLevel = LogLevel::Debug;
            continue;
        }
        if (a == "-vv") {
            lastLevel = LogLevel::Info;
            continue;
        }
        if (a == "-v" || a == "--verbose") {
            lastLevel = LogLevel::Warn;
            continue;
        }
    }

    Logger::SetGlobalLogThreshold(lastLevel);
    Logger::SetLogCategoryPreset(parseLogPresetToken(lastPreset));
    if (!lastInclude.empty())
        Logger::SetScopeAllowPrefixes(splitCommaScopes(lastInclude));
    else
        Logger::SetScopeAllowPrefixes({});

    if (lastLevel != LogLevel::Off) {
        Logger::SetColorEnabled(true);
        bool expected = false;
        if (stderrSinkRegistered.compare_exchange_strong(expected, true))
            Logger::AddOutputStream(&std::cerr);
    }
}

} // namespace

namespace CmdLineArgs {
Command selectedCommand = Command::Run;

namespace Global {
string logLevelArg;
} // namespace Global

namespace Run {
string outputFile               = "";
string package                  = "";
string schedular                = "";
string errorFormat              = "text";
string stdLibPath               = ""; // "" means use the default path
vector<string> includeDirs      = {}; // Include directories
vector<string> targetFiles      = {};
vector<string> passes           = {}; // from --passes comma-separated or positional
string passesOpt                = ""; // raw --passes "a,b,c"
string inputFile                = ""; // from --input (alternative to first positional as file)
string resolvedInputPath        = ""; // computed: inputFile or targetFiles[0]
vector<string> resolvedPassList = {}; // computed: passes or derived from targetFiles
vector<string> fallbackPasses   = {"std::default"};

bool profile        = false;
bool noCache        = false;
bool semanticOnly   = false;
unsigned int repeat = 1;
int maxThreads      = -1; // -1 means use the maximum number of threads
} // namespace Run

namespace Check {
bool lexical           = false;  // Whether to check lexical errors
bool syntaxOnly        = false;  // Whether to check syntax only
bool ignoreDefiFile    = false;  // Whether to ignore the definition file
string outputFormat    = "text"; // Output format (text or json)
string configFilePath  = "";     // Configuration file path
unsigned int maxWaring = 1000;   // Max warnings
} // namespace Check

namespace Inspect {
bool dumpTokens = false; // Whether to dump tokens
bool dumpCST    = false; // Whether to dump Concrete Syntax Tree
bool dumpAST    = false; // Whether to dump Abstract Syntax Tree
bool dumpGCT    = false; // Whether to dump Graph Construction Tree
bool dumpGIR    = false; // Whether to dump Graph Intermediate Representation
bool dumpTNS    = false; // Whether to dump Topological Node Sequence
bool geneCode   = false; // Whether to generate code from GIR
int passUntil   = -1;    // Pass until the given pass
} // namespace Inspect

} // namespace CmdLineArgs

using namespace CmdLineArgs;
using namespace CmdLineArgs::Run;
using namespace CmdLineArgs::Check;
using namespace CmdLineArgs::Inspect;

void _printCheckArgs() {
    cout << "Check args: " << endl;
    cout << "\tlexical-only:" << lexical << endl;
    cout << "\tsyntax-only:" << syntaxOnly << endl;
    cout << "\toutput-format:" << outputFormat << endl;
    cout << "\tmax-warning:" << maxWaring << endl;
    cout << "\tconfig-file:" << configFilePath << endl;
    cout << "\tignore:" << Check::ignoreDefiFile << endl;
    cout << "\ttarget-file:";
    for (auto &file : targetFiles) {
        cout << file << " ";
    }
    cout << endl;
}

void _printInspectArgs() {
    cout << "Inspect args: " << endl;
    cout << "\tdump-tokens:" << dumpTokens << endl;
    cout << "\tdump-cst:" << dumpCST << endl;
    cout << "\tdump-ast:" << dumpAST << endl;
    cout << "\tdump-gct:" << dumpGCT << endl;
    cout << "\tdump-gir:" << dumpGIR << endl;
    cout << "\tpass-until:" << passUntil << endl;
    cout << "\ttarget-file:";
    for (auto &file : targetFiles) {
        cout << file << " ";
    }
    cout << endl;
}

void printCliArgs(Command selected) {
    switch (selected) {
    case Command::Check:
        _printCheckArgs();
        break;
    case Command::Inspect:
        _printInspectArgs();
        break;
    default:
        break;
    }
}

bool parseArgs(int argc, char *argv[]) {
    bool showVersion = false; // Version information
    bool showHelp    = false; // Help information
    bool showDocs    = false; // Documentation information
    bool showAbout   = false; // About information
    bool showZen     = false; // Show Zen of Camel

    std::string logPresetConsumed;
    std::string logIncludeConsumed;

    auto globalOps =
        (option("-vvvv") % "log threshold: trace (shortcut)",
         option("-vvv") % "log threshold: debug (shortcut)",
         option("-vv") % "log threshold: info (shortcut)",
         (option("-v", "--verbose") % "log threshold: warn (shortcut)"),
         ((option("-l", "--log-level") & value("level", Global::logLevelArg)) %
          "fatal|warn|info|debug|trace|off (default: fatal)"),
         ((option("--log-preset") & value("preset", logPresetConsumed)) %
          "scope filter preset: none|wall|extra"),
         ((option("--log-include") & value("scopes", logIncludeConsumed)) %
          "comma-separated scope prefixes (overrides preset list)"));

    auto run =
        (option("-P", "--profile").set(profile) % "profile the perf",
         (option("-S", "--scheduler") & value("schedular type", schedular)) % "scheduler type",
         (option("-t", "--threads") & integer("max threads", maxThreads)) % "max threads",
         option("-n", "--no-cache").set(noCache) % "do not use cache",
         (option("-r", "--repeat") & integer("repeat times", repeat)) % "repeat times",
         (option("-I", "--include") & values("include dir", includeDirs)) % "add include directory",
         (option("-L", "--stdlib") & value("stdlib path", stdLibPath)) % "add stdlib path",
         (option("-E", "--error-format") & value("error format", errorFormat)) %
             "error format: text or json",
         (option("--passes") & value("pass1,pass2,...", passesOpt)) %
             "pass list as comma-separated (e.g. --passes std::gir,other)",
         (option("--input") & value("input file", inputFile)) %
             "input file (optional if positional given)",
         globalOps,
         opt_values("input", targetFiles) %
             "input file (optional if --input given; at least one required)");

    auto info =
        (option("-V", "--version").set(showVersion).set(selectedCommand, Command::Info) %
             "show version",
         option("-h", "--help").set(showHelp).set(selectedCommand, Command::Info) %
             "show this help message",
         option("-d", "--docs").set(showDocs).set(selectedCommand, Command::Info) %
             "show documentation",
         option("-a", "--about").set(showAbout).set(selectedCommand, Command::Info) %
             "show copyright and related information",
         option("-z", "--zen").set(showZen).set(selectedCommand, Command::Info) %
             "show the Zen of Camel");

    auto check =
        (command("check").set(selectedCommand, Command::Check) % "check the code",
         option("-i", "--lexical-only").set(lexical) % "indentation size in spaces",
         option("-s", "--syntax-only").set(syntaxOnly) % "syntax only",
         (option("-O", "--output-format") & value("output format", outputFormat)) %
             "output format: text or json",
         (option("-N", "--max-warning") & integer("max warnings", maxWaring)) % "max warnings",
         (option("-c", "--config") & value("config file path", configFilePath)) %
             "config file path",
         option("-e", "--ignore").set(Check::ignoreDefiFile) % "ignore the definition file",
         option("-o", "--output") & value("output file", outputFile = "stdout") % "output file",
         globalOps,
         values("input", targetFiles) % "input file");

    auto inspect =
        (command("inspect").set(selectedCommand, Command::Inspect) % "inspect the code",
         joinable(
             option("-t", "-T").set(dumpTokens) % "dump tokens",
             option("-s", "-S").set(dumpCST) % "dump concrete syntax tree",
             option("-a", "-A").set(dumpAST) % "dump abstract syntax tree",
             option("-c", "-C").set(dumpGCT) % "dump graph construct tree",
             option("-g", "-G").set(dumpGIR) % "dump graph intermediate representation"),
         option("--tok", "--token-stream").set(dumpTokens) % "dump tokens",
         option("--cst", "--concrete-syntax-tree").set(dumpCST) % "dump concrete syntax tree",
         option("--ast", "--abstract-syntax-tree").set(dumpAST) % "dump abstract syntax tree",
         option("--gct", "--graph-construct-tree").set(dumpGCT) % "dump graph construct tree",
         option("--gir", "--graph-ir").set(dumpGIR) % "dump graph intermediate representation",
         option("--tns", "--topo-node-seq").set(dumpTNS) % "dump topological sorted node sequence",
         option("--gen", "--gene-code").set(geneCode) % "generate code from AST",
         (option("-p", "-P", "--pass-until") & integer("pass until", passUntil)) %
             "pass until the given pass",
         globalOps,
         values("input", targetFiles) % "input file");

    auto cli = run | info | check | inspect;

    if (!parse(argc, argv, cli)) {
        cout << "Usage: " << endl;
        cout << "\tcamel [options] [<target file>]  (run: need --input or positional)\n";
        cout << usage_lines(cli, "camel") << endl;
        return false;
    }

    applyCamelCliLogging(argc, argv);

    if (showVersion) {
#ifdef NDEBUG
        cout << "Camel v" << VERSION << " (Build " << BUILD_FOOTPRINT << ")" << endl;
#else
        cout << "Camel v" << VERSION << " (Debug " << BUILD_FOOTPRINT << ")" << endl;
#endif
    }

    if (showHelp) {
        cout << make_man_page(cli, "camel");
    }

    if (showDocs) {
        cout << "Not implemented yet" << endl;
    }

    if (showAbout) {
#ifdef NDEBUG
        cout << "Camel v" << VERSION << " (Build " << BUILD_FOOTPRINT << ")";
#else
        cout << "Camel v" << VERSION << " (Debug " << BUILD_FOOTPRINT << ")";
#endif
        cout <<
#include "ABOUT"
            ;
    }

    if (showZen) {
        cout <<
#include "ZEN"
            ;
    }

    if (selectedCommand == Command::Info) {
        return false;
    }

    // Resolve input path and pass list so main does not branch on --input/--passes
    if (selectedCommand == Command::Run || selectedCommand == Command::Inspect ||
        selectedCommand == Command::Check) {
        Run::resolvedInputPath = Run::inputFile.empty()
                                     ? (Run::targetFiles.empty() ? "" : Run::targetFiles[0])
                                     : Run::inputFile;
    }
    if (selectedCommand == Command::Run) {
        if (!Run::passesOpt.empty()) {
            for (auto part : strutil::split(Run::passesOpt, ',')) {
                std::string s(strutil::trim(part));
                if (!s.empty())
                    Run::passes.push_back(std::move(s));
            }
        }
        if (!Run::passes.empty()) {
            Run::resolvedPassList = Run::passes;
        } else if (!Run::inputFile.empty()) {
            Run::resolvedPassList = Run::targetFiles;
        } else {
            Run::resolvedPassList.assign(Run::targetFiles.begin() + 1, Run::targetFiles.end());
        }

        // Apply .opencmlrc: list = prefix + CLI passes + suffix; fallback used only when graph !=
        // null after list.
        string cwd = fs::current_path().string();
        string scriptDir =
            Run::resolvedInputPath.empty()
                ? ""
                : fs::absolute(fs::path(Run::resolvedInputPath)).parent_path().string();
        Run::resolvedPassList = opencmlrc::resolvePassListWithRc(
            cwd,
            scriptDir,
            Run::resolvedPassList,
            &Run::fallbackPasses);
    }

    // Run requires at least one input source: --input or positional file
    if (selectedCommand == Command::Run && Run::resolvedInputPath.empty()) {
        std::cerr
            << "Error: Run requires an input file. Use --input <file> or provide a positional file."
            << std::endl;
        return false;
    }

    return true;
}
