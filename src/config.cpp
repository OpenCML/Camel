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
 * Updated: Feb. 06, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>

#include "clipp.h"
#include "config.h"

using namespace clipp;
using namespace std;

string targetFile = "";
string outputFile = "";
std::string errorFormat = "text";
vector<string> includeDirs = {};
vector<string> scriptsDirs = {};
bool profile = false;
bool verbose = false;
bool noCache = false;
bool syntaxOnly = false;
bool semanticOnly = false;
bool dumpCST = false, dumpAST = false, dumpGIR = false, dumpONNX = false, dumpTokens = false, format = false;
unsigned int repeat = 1;
unsigned int maxThreads = 1;
unsigned int serverPort = 0;

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
    bool showHelp = false;
    bool showVersion = false;
    bool printArgs = false;

    auto cli =
        ((option("-h", "--help").set(showHelp).doc("show this help message"),
          option("-v", "--version").set(showVersion).doc("show version")) |
         ((option("-o", "--output") & value("output file", outputFile)).doc("output file"),
          (option("-r", "--repeat") & integer("repeat", repeat)).doc("repeat times"),
          (option("-t", "--threads") & integer("max threads", maxThreads)).doc("max threads"),
          (option("-p", "--port") & integer("server port", serverPort)).doc("server port"),
          option("-g", "--verbose").set(verbose).doc("show verbose information"),
          option("-n", "--no-cache").set(noCache).doc("do not use cache"),
          option("--syntax-only").set(syntaxOnly).doc("syntax only"),
          option("--semantic-only").set(semanticOnly).doc("semantic only"),
          (option("-e", "--error-format") & value("error format", errorFormat)).doc("error format"),
          option("-P", "--profile").set(profile).doc("profile the perf"),
          (option("-I", "--include") & values("include dir", includeDirs)).doc("add include directory"),
          (option("-S", "--scripts") & values("extern scripts dir", scriptsDirs)).doc("add scripts directory"),
          option("-T", "--dump-tok").set(dumpTokens).doc("dump tokens"),
          option("-C", "--dump-cst").set(dumpCST).doc("dump concrete syntax tree"),
          option("-A", "--dump-ast").set(dumpAST).doc("dump abstract syntax tree"),
          option("-G", "--dump-gir").set(dumpGIR).doc("dump graph intermediate representation"),
          option("-X", "--dump-onnx").set(dumpGIR).doc("dump ONNX model"),
          option("-F", "--format").set(format).doc("format the code"),
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
