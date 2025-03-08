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
 * Updated: Feb. 08, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>

#include "clipp/clipp.h"
#include "config.h"

using namespace clipp;
using namespace std;
using namespace Format;  // Form at command-line parameters
using namespace Check;   // Check at command-line parameters
using namespace Inspect; // Inspect at command-line parameters
using namespace Build;   // Build at command-line parameters
using namespace Debug;   // Debug at command-line parameters
string targetFile = "";
string outputFile = "";
std::string errorFormat = "text";
vector<string> scriptsDirs = {};
bool profile = false;
bool noCache = false;
bool semanticOnly = false;
unsigned int repeat = 1;
unsigned int maxThreads = 1;

void _printArgs() {
    cout << "Target file: " << targetFile << endl;
    cout << "Output file: " << outputFile << endl;
    cout << "Error format: " << errorFormat << endl;
    cout << "Include dirs: ";
    for (auto &dir : Build::includeDirs) {
        cout << dir << " ";
    }
    cout << endl;
    cout << "Scripts dirs: ";
    for (auto &dir : scriptsDirs) {
        cout << dir << " ";
    }
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
    bool printArgs = false;
    bool showCopyRightInfo = false;   // Copyright information
    Command selected = Command::help; // Selected mode(default to help)
    auto format = (command("format").set(selected, Command::format).doc("format the code"),
                   value("input", targetFile).doc("input file"),
                   (option("-t", "--tab-size") & integer("tabsize", tabSize)).doc("indentation size in spaces"),
                   option("-u", "--use-tabs").set(useTabs).doc("use tabs instead of spaces for indentation"),
                   (option("-q", "--quote-prefer") & value(quotePrefer)).doc("quote preference: single or double"),
                   (option("-m", "--max-width") & integer("max width", maxWidth)).doc("max line width"),
                   (option("-c", "--config") & value(configFile)).doc("config file path"),
                   option("--ignore").set(Format::ignore).doc("ignore the definition file"),
                   option("-i", "--inplace").set(inplace).doc("modify the input file in place"));
    auto check = (command("check").set(selected, Command::check).doc("check the code"),
                  value("input", targetFile).doc("input file"),
                  option("-I", "--lexical-only").set(lexical).doc("indentation size in spaces"),
                  option("--syntax-only").set(syntaxOnly).doc("syntax only"),
                  (option("-O", "--output-format") & value(outputFormat)).doc("output format: text or json"),
                  (option("-N", "--max-warning") & integer("max warnings", maxWaring)).doc("max warnings"),
                  (option("-c", "--config") & value("config file path", configFilePath)).doc("config file path"),
                  option("-e", "--ignore").set(Check::ignore).doc("ignore the definition file"),
                  option("-o", "--output") & value("output file", outputFile = "console").doc("output file"));
    auto inspect =
        (command("inspect").set(selected, Command::inspect).doc("inspect the code"),
         value("input", targetFile).doc("input file"), option("-tT", "--tokens").set(dumpTokens).doc("dump tokens"),
         option("-sS", "--cst", "--syntax-tree").set(dumpCST).doc("dump concrete syntax tree"),
         option("-aA", "--ast", "--abstract-tree").set(dumpAST).doc("dump abstract syntax tree"),
         option("-cC", "--gct", "--graph-tree").set(dumpGCT).doc("dump graph constraction tree"),
         option("-gG", "--gir", "--graph").set(dumpGIR).doc("dump graph intermediate representation"),
         (option("-pP", "--pass-until") & integer("pass until", passUntil)).doc("pass until the given pass"));
    auto build =
        (command("build").set(selected, Command::build).doc("build the code"),
         value("input", targetFile).doc("input file"),
         option("-o", "-O", "--optimize").set(optimize).doc("optimize the code"),
         option("-r", "-R", "-rollup").set(rollup).doc("rollup the code"),
         option("-g", "--verbose").set(verbose).doc("show verbose information"),
         (option("-W", "--warning") & value("warning switch", warningSwitch)).doc("warning switch"),
         (option("--output") & (rollup ? value("output dirctary", outputDir) : value("output file", outputFile)))
             .doc("output file or directory"),
         option("--include") & values("include dir", Build::includeDirs).doc("add include directory"),
         (option("--stdlib") & value("stdlib dir", Build::stdLibPath)).doc("add stdlib path(default: ./stdlib)"));
    auto serve =
        (command("serve").set(selected, Command::serve).doc(" the code"), value("input", targetFile).doc("input file"),
         (option("--host") & value("host", serverHost)).doc("host"),
         (option("--port") & integer("port", serverPort)).doc("port"));
    auto debug =
        (command("debug").set(selected, Command::debug).doc("debug the code"),
         value("input", targetFile).doc("input file"),
         option("--include") & values("include dir", Debug::includeDirs).doc("add include directory"),
         (option("--stdlib") & value("stdlib dir", Debug::stdLibPath)).doc("add stdlib path(default: ./stdlib)"));
    auto cli =
        (format | check | inspect | build |
         (option("-h", "--help").set(showHelp).doc("show this help message"),
          option("-v", "--version").set(showVersion).doc("show version"),
          option("-a", "--about").set(showCopyRightInfo).doc("show copyright and related information")) |
         ((option("-o", "--output") & value("output file", outputFile)).doc("output file"),
          (option("-r", "--repeat") & integer("repeat", repeat)).doc("repeat times"),
          (option("-t", "--threads") & integer("max threads", maxThreads)).doc("max threads"),
          (option("-p", "--port") & integer("server port", serverPort)).doc("server port"),
          option("-g", "--verbose").set(verbose).doc("show verbose information"),
          option("-n", "--no-cache").set(noCache).doc("do not use cache"),
          option("--semantic-only").set(semanticOnly).doc("semantic only"),
          (option("-e", "--error-format") & value("error format", errorFormat)).doc("error format"),
          option("-P", "--profile").set(profile).doc("profile the perf"),
          (option("-S", "--scripts") & values("extern scripts dir", scriptsDirs)).doc("add scripts directory"),
          option("-R", "--print-args").set(printArgs).doc("print arguments"), opt_value("target file", targetFile)));

    if (!parse(argc, argv, cli)) {
        cout << "Usage: " << endl;
        cout << "\tcamel [options] <target file> \n";
        cout << usage_lines(cli, "camel") << endl;
        return false;
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
