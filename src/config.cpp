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
 * Updated: Oct. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>

#include "clipp/clipp.h"
#include "config.h"
#include "utils/log.h"
#include "utils/str.h"

#ifndef BUILD_FOOTPRINT
#define BUILD_FOOTPRINT "%y%m%d_%H%M%S"
#endif

using namespace clipp;
using namespace std;

namespace CmdLineArgs {
Command selectedCommand = Command::Run;

namespace Global {
bool verbose = false;
string logLevel = "off";
} // namespace Global

namespace Run {
string outputFile = "";
string package = "";
string schedular = "";
string errorFormat = "text";
string stdLibPath = "";          // "" means use the default path
vector<string> includeDirs = {}; // Include directories
vector<string> targetFiles = {};

bool profile = false;
bool noCache = false;
bool semanticOnly = false;
unsigned int repeat = 1;
int maxThreads = -1; // -1 means use the maximum number of threads
} // namespace Run

namespace Format {
string quotePrefer = "";     // Quote preference (default to single quotes)
string configFile = "";      // Configuration file path
bool useTabs = false;        // Whether to use tabs for indentation
bool inplace = false;        // Whether to modify the input file in place
bool ignoreDefiFile = false; // Whether to ignore the definition file
unsigned int tabSize = 4;    // Indentation size in spaces
unsigned int maxWidth = 80;  // Max line width
}; // namespace Format

namespace Check {
bool lexical = false;          // Whether to check lexical errors
bool syntaxOnly = false;       // Whether to check syntax only
bool ignoreDefiFile = false;   // Whether to ignore the definition file
string outputFormat = "text";  // Output format (text or json)
string configFilePath = "";    // Configuration file path
unsigned int maxWaring = 1000; // Max warnings
} // namespace Check

namespace Inspect {
bool dumpTokens = false; // Whether to dump tokens
bool dumpCST = false;    // Whether to dump Concrete Syntax Tree
bool dumpAST = false;    // Whether to dump Abstract Syntax Tree
bool dumpGCT = false;    // Whether to dump Graph Construction Tree
bool dumpGIR = false;    // Whether to dump Graph Intermediate Representation
bool dumpTNS = false;    // Whether to dump Topological Node Sequence
bool geneCode = false;   // Whether to generate code from GIR
int passUntil = -1;      // Pass until the given pass
} // namespace Inspect

} // namespace CmdLineArgs

using namespace CmdLineArgs;
using namespace CmdLineArgs::Run;
using namespace CmdLineArgs::Format;
using namespace CmdLineArgs::Check;
using namespace CmdLineArgs::Inspect;

void _printFormatArgs() {
    cout << "Format args: " << endl;
    cout << "\ttab-size:" << tabSize << endl;
    cout << "\tuse-tabs:" << useTabs << endl;
    cout << "\tquote-prefer:" << quotePrefer << endl;
    cout << "\tmax-width:" << maxWidth << endl;
    cout << "\tconfig-file:" << configFile << endl;
    cout << "\tignore:" << Format::ignoreDefiFile << endl;
    cout << "\tinplace:" << inplace << endl;
    cout << "\ttarget-file:";
    for (auto &file : targetFiles) {
        cout << file << " ";
    }
    cout << endl;
}

void _printCheckArgs() {
    cout << "Check args: " << endl;
    cout << "\tlexical-only:" << lexical << endl;
    cout << "\tsyntax-only:" << syntaxOnly << endl;
    cout << "\toutput-format:" << outputFormat << endl;
    cout << "\tmax-warning:" << maxWaring << endl;
    cout << "\tconfig-file:" << configFile << endl;
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
    case Command::Format:
        _printFormatArgs();
        break;
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
    bool showHelp = false;    // Help information
    bool showDocs = false;    // Documentation information
    bool showAbout = false;   // About information
    bool showZen = false;     // Show Zen of Camel

    auto globalOps =
        ((option("-v", "--verbose").set(Global::verbose) % "enable verbose output"),
         ((option("-l", "--log-level") & value("level", Global::logLevel)) %
          "set log level: debug, info, warn, error, off (default to info)"));

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
         globalOps,
         values("input", targetFiles) % "input file");

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

    auto format =
        (command("format").set(selectedCommand, Command::Format) % "format the code",
         (option("-t", "--tab-size") & integer("tabsize", tabSize)) % "indentation size in spaces",
         option("-u", "--use-tabs").set(useTabs) % "use tabs instead of spaces for indentation",
         (option("-q", "--quote-prefer") & value("quote preference", quotePrefer = "single")) %
             "quote preference: single or double",
         (option("-m", "--max-width") & integer("max width", maxWidth)) % "max line width",
         (option("-c", "--config") & value("config file path", configFile)) % "config file path",
         option("--ignore").set(Format::ignoreDefiFile) % "ignore the definition file",
         option("-i", "--inplace").set(inplace) % "modify the input file in place",
         globalOps,
         values("input", targetFiles) % "input file");

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

    auto cli = run | info | format | check | inspect;

    if (!parse(argc, argv, cli)) {
        cout << "Usage: " << endl;
        cout << "\tcamel [options] <target file> \n";
        cout << usage_lines(cli, "camel") << endl;
        return false;
    }

    if (Global::verbose) {
        Logger::SetVerbose(true);
        if (!Global::logLevel.empty()) {
            std::string level{strutil::trim(Global::logLevel)};
            std::transform(level.begin(), level.end(), level.begin(), ::tolower);

            if (level == "debug")
                Logger::SetLogLevel(Logger::Level::Debug);
            else if (level == "info")
                Logger::SetLogLevel(Logger::Level::Info);
            else if (level == "warn")
                Logger::SetLogLevel(Logger::Level::Warn);
            else if (level == "error")
                Logger::SetLogLevel(Logger::Level::Error);
            else if (level == "off")
                Logger::SetLogLevel(Logger::Level::Off);
            else {
                std::cerr << "Unknown log level: '" << level << "'" << std::endl;
                std::exit(1);
            }
        } else {
            Logger::SetLogLevel(Logger::Level::Info);
        }
    }

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

    return true;
}
