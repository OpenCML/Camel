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
 * Updated: Mar. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */
#include <iostream>

#include "clipp/clipp.h"
#include "config.h"

using namespace clipp;
using namespace std;

namespace CmdLineArgs {

namespace Format {
string quotePrefer = "single"; // Quote preference (default to single quotes)
string configFile = "";        // Configuration file path
bool useTabs = false;          // Whether to use tabs for indentation
bool inplace = false;          // Whether to modify the input file in place
bool ignoreDefiFile = false;   // Whether to ignore the definition file
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
bool dumpONNX = false;   // Whether to dump ONNX
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


string targetFile = "";
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
unsigned int repeat = 1;
unsigned int maxThreads = 1;

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
}

void _printCheckArgs() {
    cout << "Check args: " << endl;
    cout << "\tlexical-only:" << lexical << endl;
    cout << "\tsyntax-only:" << syntaxOnly << endl;
    cout << "\toutput-format:" << outputFormat << endl;
    cout << "\tmax-warning:" << maxWaring << endl;
    cout << "\tconfig-file:" << configFile << endl;
    cout << "\tignore:" << Check::ignoreDefiFile << endl;
}

void _printInspectArgs() {
    cout << "Inspect args: " << endl;
    cout << "\tdump-tokens:" << dumpTokens << endl;
    cout << "\tdump-cst:" << dumpCST << endl;
    cout << "\tdump-ast:" << dumpAST << endl;
    cout << "\tdump-gct:" << dumpGCT << endl;
    cout << "\tdump-gir:" << dumpGIR << endl;
    cout << "\tdump-onnx:" << dumpONNX << endl;
    cout << "\tpass-until:" << passUntil << endl;
}

void _printBuildArgs() {
    cout << "Build args: " << endl;
    cout << "\toptimize:" << optimize << endl;
    cout << "\trollup:" << rollup << endl;
    cout << "\tverbose:" << verbose << endl;
    cout << "\twarning-switch:" << warningSwitch << endl;
    cout << "\toutput-dir:" << outputDir << endl;
    cout << "\tstd-lib-path:" << stdLibPath << endl;
}

void _printDebugArgs() {
    cout << "Debug args: " << endl;
    cout << "\tvariable:" << variable << endl;
    cout << "\tincludeDir:" << endl;
    for (auto &dir : includeDirs) {
        cout << dir << endl;
    }
    cout << "\tstd-lib-path:" << stdLibPath << endl;
}

void _printServeArgs() {
    cout << "Serve args: " << endl;
    cout << "\tserver-host:" << serverHost << endl;
    cout << "\tserver-port:" << serverPort << endl;
}

void _printHelpArgs() {
    cout << "Help args: " << endl;
    cout << "\tprofile:" << profile << endl;
    cout << "\tno-cache:" << noCache << endl;
    cout << "\tsemantic-only:" << semanticOnly << endl;
    cout << "\trepeat:" << repeat << endl;
    cout << "\tincludeDir:" << endl;
    for (auto &dir : includeDirs) {
        cout << dir << endl;
    }
    cout << "\tstd-lib-path:" << stdLibPath << endl;
}

void _printArgs() {
    cout << "Target file: " << targetFile << endl;
    cout << "Output file: " << outputFile << endl;
    cout << "Error format: " << errorFormat << endl;
    cout << "Include dirs: ";
    for (auto &dir : includeDirs) {
        cout << dir << " ";
    }
    cout << endl;
    cout << "Scripts dirs: ";
    /*for (auto &dir : scriptsDirs) {
        cout << dir << " ";
    }*/
    cout << endl;
    cout << "Verbose: " << verbose << endl;
    cout << "No cache: " << noCache << endl;
    cout << "Syntax only: " << syntaxOnly << endl;
    cout << "Semantic only: " << semanticOnly << endl;
    cout << "Dump CST: " << dumpCST << endl;
    cout << "Dump AST: " << dumpAST << endl;
    cout << "Dump GIR: " << dumpGIR << endl;
    cout << "Profile: " << profile << endl;
    cout << "Repeat: " << repeat << endl;
    cout << "Max threads: " << maxThreads << endl;
    cout << "Server port: " << serverPort << endl;
}

bool parseArgs(int argc, char *argv[]) {
    bool showHelp = false;    // Help information
    bool showVersion = false; // Version information
    bool showDocs = false;
    bool printArgs = false;
    bool showCopyRightInfo = false;   // Copyright information
    Command selected = Command::HELP; // Selected mode(default to help)
    auto format = (command("format").set(formatCode).set(selected, Command::FORMAT).doc("format the code"),
                   (option("-t", "--tab-size") & integer("tabsize", tabSize)).doc("indentation size in spaces"),
                   option("-u", "--use-tabs").set(useTabs).doc("use tabs instead of spaces for indentation"),
                   (option("-q", "--quote-prefer") & value(quotePrefer)).doc("quote preference: single or double"),
                   (option("-m", "--max-width") & integer("max width", maxWidth)).doc("max line width"),
                   (option("-c", "--config") & value(configFile)).doc("config file path"),
                   option("--ignore").set(Format::ignoreDefiFile).doc("ignore the definition file"),
                   option("-i", "--inplace").set(inplace).doc("modify the input file in place"),
                   values("input", targetFiles).doc("input file"));
    auto check = (command("check").set(selected, Command::CHECK).doc("check the code"),
                  option("-I", "--lexical-only").set(lexical).doc("indentation size in spaces"),
                  option("--syntax-only").set(syntaxOnly).doc("syntax only"),
                  (option("-O", "--output-format") & value(outputFormat)).doc("output format: text or json"),
                  (option("-N", "--max-warning") & integer("max warnings", maxWaring)).doc("max warnings"),
                  (option("-c", "--config") & value("config file path", configFilePath)).doc("config file path"),
                  option("-e", "--ignore").set(Check::ignoreDefiFile).doc("ignore the definition file"),
                  option("-o", "--output") & value("output file", outputFile = "console").doc("output file"),
                  values("input", targetFiles).doc("input file"));

    auto inspect =
        (command("inspect").set(selected, Command::INSPECT).doc("inspect the code"),
         option("-tT", "--tokens").set(dumpTokens).doc("dump tokens"),
         option("-s", "-S", "--cst", "--syntax-tree").set(dumpCST).doc("dump concrete syntax tree"),
         option("-a", "-A", "--ast", "--abstract-tree").set(dumpAST).doc("dump abstract syntax tree"),
         option("-c", "-C", "--gct", "--graph-tree").set(dumpGCT).doc("dump graph constraction tree"),
         option("-g", "-G", "--gir", "--graph").set(dumpGIR).doc("dump graph intermediate representation"),
         (option("-p", "-P", "--pass-until") & integer("pass until", passUntil)).doc("pass until the given pass"),
         values("input", targetFiles).doc("input file"));

    auto build =
        (command("build").set(selected, Command::BUILD).doc("build the code"),
         option("-o", "-O", "--optimize").set(optimize).doc("optimize the code"),
         option("-r", "-R", "-rollup").set(rollup).doc("rollup the code"),
         option("-g", "--verbose").set(verbose).doc("show verbose information"),
         (option("-W", "--warning") & value("warning switch", warningSwitch)).doc("warning switch"),
         (option("--output") & (rollup ? value("output dirctary", outputDir) : value("output file", outputFile)))
             .doc("output file or directory"),
         option("--include") & values("include dir", includeDirs).doc("add include directory"),
         (option("--stdlib") & value("stdlib dir", stdLibPath)).doc("add stdlib path(default: ./stdlib)"),
         values("input", targetFiles).doc("input file"));
    auto serve =
        (command("serve").set(selected, Command::SERVE).doc("serve the code"),
         (option("--host") & value("host", serverHost)).doc("host"),
         (option("--port") & integer("port", serverPort)).doc("port"), value("input", targetFile).doc("input file"));

    auto debug = (command("debug").set(selected, Command::DEBUG).doc("debug the code"),
                  (option("--variable") & value("variable", variable)).doc("variable"), /*not finished yet*/
                  option("--print").doc("print degug information"),                     /*not finished yet*/
                  option("--include") & values("include dir", includeDirs).doc("add include directory"),
                  (option("--stdlib") & value("stdlib dir", stdLibPath)).doc("add stdlib path(default: ./stdlib)"),
                  values("input", targetFiles).doc("input file"));
    targetFile = targetFiles[0];

    auto cli =
        (format | check | inspect | build | debug |
             (option("-h", "--help").set(showHelp).doc("show this help message"),
              option("-v", "--version").set(showVersion).doc("show version"),
              option("-a", "--about").set(showCopyRightInfo).doc("show copyright and related information"),
              (option("--doc").set(showDocs) & value("package name", package)).doc("show document of the package")) |
             option("--profile").set(profile).doc("profile the perf"),
         (option("--scheduler") & value("schedular type", schedular)).doc("scheduler type"),
         (option("-t", "--threads") & integer("max threads", maxThreads)).doc("max threads"),
         option("-n", "--no-cache").set(noCache).doc("do not use cache"),
         (option("-r", "--repeat") & integer("repeat times", repeat)).doc("repeat times"),
         (option("--include") & values("include dir", includeDirs)).doc("add include directory"),
         (option("--stdlib") & value("stdlib dir", stdLibPath)).doc("add stdlib path(default:./stdlib)"));
    if (!parse(argc, argv, cli)) {
        cout << "Usage: " << endl;
        cout << "\tcamel [options] <target file> \n";
        cout << usage_lines(cli, "camel") << endl;
        return false;
    }
    switch (selected) {
    case Command::HELP:
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
    if (showHelp) {
        cout << make_man_page(cli, "camel");
        return false;
    }
    if (showVersion) {
#ifdef NDEBUG
        cout << "Camel v" << VERSION << endl;
#else
        cout << "Camel (DEBUG) v" << VERSION << endl;
#endif
        return false;
    }
    if (printArgs) {
        _printArgs();
    }

    return true;
}

#ifdef DEBUGING
int main(int argc, char *argv[]) {
    parseArgs(argc, argv);
    return 0;
}
#endif
