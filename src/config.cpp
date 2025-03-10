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
 * Created: Mar. 17, 2024
 * Updated: Mar. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */
#include <iostream>

#include "clipp/clipp.h"
#include "config.h"

using namespace clipp;
using namespace std;

namespace CmdLineArgs {
Command selectedCommand = Command::RUN;

string outputFile = "";
string package = "";
string schedular = "";
string errorFormat = "text";
string stdLibPath = "./stdlib";
vector<string> includeDirs = {}; // Include directories
vector<string> targetFiles = {};

bool profile = false;
bool noCache = false;
bool semanticOnly = false;
bool setThreads = false;
bool setRepeat = false;
bool setStdLibPath = false;
unsigned int repeat = 1;
unsigned int maxThreads = 1;

namespace Format {
string quotePrefer = "";     // Quote preference (default to single quotes)
string configFile = "";      // Configuration file path
bool useTabs = false;        // Whether to use tabs for indentation
bool inplace = false;        // Whether to modify the input file in place
bool ignoreDefiFile = false; // Whether to ignore the definition file
bool formatCode = false;
unsigned int tabSize = 4;   // Indentation size in spaces
unsigned int maxWidth = 80; // Max line width
}; // namespace Format

namespace Check {
bool lexical = false;          // Whether to check lexical errors
bool syntaxOnly = false;       // Whether to check syntax only
bool ignoreDefiFile = false;   // Whether to ignore the definition file
string outputFormat = "json";  // Output format (default to JSON)
string configFilePath = "";    // Configuration file path
unsigned int maxWaring = 1000; // Max warnings
} // namespace Check

namespace Inspect {
bool dumpTokens = false; // Whether to dump tokens
bool dumpCST = false;    // Whether to dump CST
bool dumpAST = false;    // Whether to dump AST
bool dumpGCT = false;    // Whether to dump GCT
bool dumpGIR = false;    // Whether to dump GIR
int passUntil = -1;      // Pass until the given pass
} // namespace Inspect

namespace Build {
bool optimize = false;       // Whether to optimize the code
bool rollup = false;         // Whether to rollup the code
bool verbose = false;        // Whether to show verbose information
string warningSwitch = "on"; // Warning switch (default to on)
string outputDir = "";       // Output directory
} // namespace Build

namespace Serve {
string serverHost = "";  // Server host
unsigned int serverPort; // Server port
} // namespace Serve

namespace Debug {
string variable = ""; // Whether to optimize the code
} // namespace Debug
} // namespace CmdLineArgs

using namespace CmdLineArgs;
using namespace CmdLineArgs::Format;
using namespace CmdLineArgs::Check;
using namespace CmdLineArgs::Inspect;
using namespace CmdLineArgs::Build;
using namespace CmdLineArgs::Debug;
using namespace CmdLineArgs::Serve;

void _printFormatArgs() {
    cout << "Format args: " << endl;
    cout << "\ttab-size:" << tabSize << endl;
    cout << "\tuse-tabs:" << useTabs << endl;
    cout << "\tquote-prefer:" << quotePrefer << endl;
    cout << "\tmax-width:" << maxWidth << endl;
    cout << "\tconfig-file:" << configFile << endl;
    cout << "\tignore:" << Format::ignoreDefiFile << endl;
    cout << "\tinplace:" << inplace << endl;
    cout << "\tformat-code:" << formatCode << endl;
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

void _printBuildArgs() {
    cout << "Build args: " << endl;
    cout << "\toptimize:" << optimize << endl;
    cout << "\trollup:" << rollup << endl;
    cout << "\tverbose:" << verbose << endl;
    cout << "\twarning-switch:" << warningSwitch << endl;
    cout << "\toutput-dir:" << outputDir << endl;
    cout << "\tstd-lib-path:" << stdLibPath << endl;
    if (!includeDirs.empty()) {
        cout << "\tinclude-dir:" << endl;
        for (auto &dir : includeDirs) {
            cout << dir << endl;
        }
    }
    cout << "\ttarget-file:";
    for (auto &file : targetFiles) {
        cout << file << " ";
    }
    cout << endl;
}

void _printDebugArgs() {
    cout << "Debug args: " << endl;
    cout << "\tvariable:" << variable << endl;
    if (!includeDirs.empty()) {
        cout << "\tincludeDir:" << endl;
        for (auto &dir : includeDirs) {
            cout << dir << endl;
        }
    }
    cout << "\tstd-lib-path:" << stdLibPath << endl;
    cout << "\ttarget-file:";
    for (auto &file : targetFiles) {
        cout << file << " ";
    }
    cout << endl;
}

void _printServeArgs() {
    cout << "Serve args: " << endl;
    cout << "\tserver-host:" << serverHost << endl;
    cout << "\tserver-port:" << serverPort << endl;
}

void _printHelpArgs() {
    cout << "Help args: " << endl;
    cout << "\tpackage:" << package << endl;
    cout << "\tprofile:" << profile << endl;
    cout << "\tscheduler:" << schedular << endl;
    cout << "\tmax-threads:" << maxThreads << endl;
    cout << "\tno-cache:" << noCache << endl;
    cout << "\tsemantic-only:" << semanticOnly << endl;
    cout << "\trepeat:" << repeat << endl;
    if (!includeDirs.empty()) {
        cout << "\tincludeDir:" << endl;
        for (auto &dir : includeDirs) {
            cout << dir << endl;
        }
    }
    cout << "\tstd-lib-path:" << stdLibPath << endl;
}

void printCliArgs(Command selected) {
    switch (selected) {
    case Command::INFO:
        _printHelpArgs();
        break;
    case Command::FORMAT:
        _printFormatArgs();
        break;
    case Command::CHECK:
        _printCheckArgs();
        break;
    case Command::INSPECT:
        _printInspectArgs();
        break;
    case Command::BUILD:
        _printBuildArgs();
        break;
    case Command::DEBUG:
        _printDebugArgs();
        break;
    case Command::SERVE:
        _printServeArgs();
        break;
    default:
        cout << "Unknown command" << endl;
        break;
    }
}

bool parseArgs(int argc, char *argv[]) {
    bool showHelp = false;    // Help information
    bool showVersion = false; // Version information
    bool showDocs = false;
    bool showCopyRightInfo = false; // Copyright information

    Command selectedCommand = Command::RUN;

    auto run = (option("-P", "--profile").set(profile) % "profile the perf",
                (option("-S", "--scheduler") & value("schedular type", schedular)) % "scheduler type",
                (option("-t", "--threads").set(setThreads) & integer("max threads", maxThreads)) % "max threads",
                option("-n", "--no-cache").set(noCache) % "do not use cache",
                (option("-r", "--repeat").set(setRepeat) & integer("repeat times", repeat)) % "repeat times",
                (option("-I", "--include") & values("include dir", includeDirs)) % "add include directory",
                (option("-L", "--stdlib").set(setStdLibPath) & value("stdlib path", stdLibPath)) % "add stdlib path",
                values("input", targetFiles) % "input file");

    auto info = (option("-h", "--help").set(showHelp).set(selectedCommand, Command::INFO) % "show this help message",
                 option("-v", "--version").set(showVersion).set(selectedCommand, Command::INFO) % "show version",
                 option("-a", "--about").set(showCopyRightInfo).set(selectedCommand, Command::INFO) %
                     "show copyright and related information");

    auto format = (command("format").set(formatCode).set(selectedCommand, Command::FORMAT) % "format the code",
                   (option("-t", "--tab-size") & integer("tabsize", tabSize)) % "indentation size in spaces",
                   option("-u", "--use-tabs").set(useTabs) % "use tabs instead of spaces for indentation",
                   (option("-q", "--quote-prefer") & value("quote preference", quotePrefer = "single")) %
                       "quote preference: single or double",
                   (option("-m", "--max-width") & integer("max width", maxWidth)) % "max line width",
                   (option("-c", "--config") & value("config file path", configFile)) % "config file path",
                   option("--ignore").set(Format::ignoreDefiFile) % "ignore the definition file",
                   option("-i", "--inplace").set(inplace) % "modify the input file in place",
                   values("input", targetFiles) % "input file");

    auto check =
        (command("check").set(selectedCommand, Command::CHECK) % "check the code",
         option("-i", "--lexical-only").set(lexical) % "indentation size in spaces",
         option("-s", "--syntax-only").set(syntaxOnly) % "syntax only",
         (option("-O", "--output-format") & value("output format", outputFormat)) % "output format: text or json",
         (option("-N", "--max-warning") & integer("max warnings", maxWaring)) % "max warnings",
         (option("-c", "--config") & value("config file path", configFilePath)) % "config file path",
         option("-e", "--ignore").set(Check::ignoreDefiFile) % "ignore the definition file",
         option("-o", "--output") & value("output file", outputFile = "console") % "output file",
         values("input", targetFiles) % "input file");

    auto inspect =
        (command("inspect").set(selectedCommand, Command::INSPECT) % "inspect the code",
         joinable(option("-t", "-T").set(dumpTokens) % "dump tokens",
                  option("-s", "-S").set(dumpCST) % "dump concrete syntax tree",
                  option("-a", "-A").set(dumpAST) % "dump abstract syntax tree",
                  option("-c", "-C").set(dumpGCT) % "dump graph construct tree",
                  option("-g", "-G").set(dumpGIR) % "dump graph intermediate representation"),
         option("--tok", "--token-stream").set(dumpTokens) % "dump tokens",
         option("--cst", "--concrete-syntax-tree").set(dumpCST) % "dump concrete syntax tree",
         option("--ast", "--abstract-syntax-tree").set(dumpAST) % "dump abstract syntax tree",
         option("--gct", "--graph-construct-tree").set(dumpGCT) % "dump graph construct tree",
         option("--gir", "--graph-intermediate-representation").set(dumpGIR) % "dump graph intermediate representation",
         (option("-p", "-P", "--pass-until") & integer("pass until", passUntil)) % "pass until the given pass",
         values("input", targetFiles) % "input file");

    auto build =
        (command("build").set(selectedCommand, Command::BUILD) % "build the code",
         option("-o", "-O", "--optimize").set(optimize) % "optimize the code",
         option("-r", "-R", "-rollup").set(rollup) % "rollup the code",
         option("-g", "--verbose").set(verbose) % "show verbose information",
         (option("-W", "--warning") & value("warning switch", warningSwitch)) % "warning switch",
         (option("--output") & (rollup ? value("output dirctary", outputDir) : value("output file", outputFile))) %
             "output file or directory",
         option("--include") & values("include dir", includeDirs) % "add include directory",
         (option("--stdlib") & value("stdlib dir", stdLibPath)) % "add stdlib path",
         values("input", targetFiles) % "input file");

    auto serve =
        (command("serve").set(selectedCommand, Command::SERVE) % "serve the code",
         (option("--host") & value("host", serverHost)) % "host",
         (option("--port") & integer("port", serverPort)) % "port", values("input", targetFiles) % "input file");

    auto debug = (command("debug").set(selectedCommand, Command::DEBUG) % "debug the code",
                  (option("--variable") & value("variable", variable)) % "variable", /*not finished yet*/
                  option("--print") % "print debug information",                     /*not finished yet*/
                  option("--include") & values("include dir", includeDirs) % "add include directory",
                  (option("--stdlib") & value("stdlib dir", stdLibPath)) % "add stdlib path",
                  values("input", targetFiles) % "input file");

    auto cli = run | info | format | check | inspect | build | debug | serve;

    if (!parse(argc, argv, cli)) {
        cout << "Usage: " << endl;
        cout << "\tcamel [options] <target file> \n";
        cout << usage_lines(cli, "camel") << endl;
        return false;
    }

#ifndef NDEBUG
    printCliArgs(selectedCommand);
#endif

    if (showHelp) {
        cout << make_man_page(cli, "camel");
    }

    if (showVersion) {
#ifdef NDEBUG
        cout << "Camel v" << VERSION << endl;
#else
        cout << "Camel (DEBUG) v" << VERSION << endl;
#endif
    }

    if (showCopyRightInfo) {
        cout << "Camel v" << VERSION << endl;
        cout << "Build: " << BUILD_TIMESTAMP << endl;
        cout << "Copyright (c) 2024 Beijing Jiaotong University" << endl;
        cout << "Camel is licensed under the MIT license." << endl;
    }

    if (showDocs) {
        cout << "Not implemented yet" << endl;
    }

    if (selectedCommand == Command::INFO || selectedCommand == Command::SERVE) {
        return false;
    }

    return true;
}