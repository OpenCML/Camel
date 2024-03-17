/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: Mar. 17, 2024
 * Supported by: National Key Research and Development Program of China
 */

#include <iostream>

#include "clipp.h"
#include "config.h"

using namespace clipp;
using namespace std;

string targetFile = "";
string outputFile = "";
vector<string> includeDirs = {};
vector<string> scriptsDirs = {};
bool profile = false;
bool verbose = false;
bool noCache = false;
bool dumpCST = false, dumpAST = false, dumpGIR = false;
unsigned int maxThreads = 1;
unsigned int serverPort = 0;

void _printArgs() {
    cout << "Target file: " << targetFile << endl;
    cout << "Output file: " << outputFile << endl;
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
    cout << "Dump CST: " << dumpCST << endl;
    cout << "Dump AST: " << dumpAST << endl;
    cout << "Dump GIR: " << dumpGIR << endl;
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
         ((option("-o", "--output") & value("output file", outputFile))
              .doc("output file"),
          (option("-t", "--threads") & integer("max threads", maxThreads))
              .doc("max threads"),
          (option("-p", "--port") & integer("server port", serverPort))
              .doc("server port"),
          option("-g", "--verbose")
              .set(verbose)
              .doc("show verbose information"),
          option("-n", "--no-cache").set(noCache).doc("do not use cache"),
          option("-p", "--profile").set(profile).doc("profile the perf"),
          (option("-I", "--include") & values("include dir", includeDirs))
              .doc("add include directory"),
          (option("-S", "--scripts") &
           values("extern scripts dir", scriptsDirs))
              .doc("add scripts directory"),
          option("-C", "--dump-cst")
              .set(dumpCST)
              .doc("dump concrete syntax tree"),
          option("-A", "--dump-ast")
              .set(dumpAST)
              .doc("dump abstract syntax tree"),
          option("-G", "--dump-gir")
              .set(dumpGIR)
              .doc("dump graph intermediate representation"),
          option("-R", "--print-args").set(printArgs).doc("print arguments"),
          value("target file", targetFile)));

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
        cout << "Camel version " << CAMEL_VERSION << endl;
        return false;
    }

    if (printArgs) {
        _printArgs();
    }

    return true;
}