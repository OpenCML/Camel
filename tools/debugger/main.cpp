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
 * Created: Feb. 22, 2026
 * Updated: Feb. 23, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "build_config.h"
#include "mem_monitor.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/listener.h"
#include "camel/core/mm.h"
#include "camel/core/module/userdef.h"
#include "camel/core/type.h"
#include "camel/execute/pass/base.h"
#include "camel/parse/antlr/OpenCMLLexer.h"
#include "camel/parse/antlr/OpenCMLParser.h"
#include "camel/parse/parse.h"
#include "camel/utils/env.h"
#include "camel/utils/dll_path.h"

#include <cstdio>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;

namespace fs = std::filesystem;

#define VERSION "0.1.0"

// Debugger state
struct DebuggerState {
    std::string targetFile;
    context_ptr_t ctx;
    std::shared_ptr<UserDefinedModule> mainModule;
    std::shared_ptr<CamelParser> parser;

    bool hasFile() const { return !targetFile.empty(); }
};

static DebuggerState g_state;
static debugger::MemMonitor g_memMonitor;

// Print help
void printHelp() {
    cout << R"(Camel Debugger - Interactive Camel debugger

Commands:
  file <path>   (f)  Load Camel source file to debug
  run          (r)  Run the loaded file
  help         (h)  Show this help
  version      (v)  Show version info
  quit         (q)  Exit debugger

Examples:
  (camel-dbg) file hello.cml
  (camel-dbg) run
  (camel-dbg) quit
)";
}

// Print version
void printVersion() {
    cout << "Camel Debugger v" << VERSION << " (Build " << BUILD_FOOTPRINT << ")" << endl;
}

//  trimmed string
static string trim(const string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

// Parse command: returns (cmd, arg)
static pair<string, string> parseCommand(const string &line) {
    string trimmed = trim(line);
    if (trimmed.empty())
        return {"", ""};

    size_t space = trimmed.find(' ');
    if (space == string::npos)
        return {trimmed, ""};

    string cmd  = trim(trimmed.substr(0, space));
    string arg  = trim(trimmed.substr(space + 1));
    return {cmd, arg};
}

// Execute file command: load file
bool cmdFile(const string &path) {
    if (path.empty()) {
        cout << "Usage: file <path>" << endl;
        return false;
    }

    if (!fs::exists(path)) {
        cout << "Error: file not found: " << path << endl;
        return false;
    }

    g_state.targetFile = path;
    cout << "Loaded: " << path << endl;
    return true;
}

// Execute run command: run loaded file
bool cmdRun() {
    if (!g_state.hasFile()) {
        cout << "Error: please load a file first with file <path>" << endl;
        return false;
    }

    const string &targetFile = g_state.targetFile;
    auto file = make_unique<std::ifstream>(targetFile);
    if (!file->is_open()) {
        cout << "Error: cannot open file " << targetFile << endl;
        return false;
    }

    auto diagnostics = make_shared<Diagnostics>("main", targetFile);
    diagnostics->setConfig(
        DiagsConfig{
            .total_limit         = -1,
            .per_severity_limits = {{Severity::Error, 0}},
        });

    fs::path camelPath = fs::current_path();
    fs::path entryPath(targetFile);
    string entryDir = fs::absolute(entryPath).parent_path().string();

    auto addIfNonEmpty = [](std::vector<std::string> &v, const std::string &s) {
        if (!s.empty())
            v.push_back(fs::absolute(fs::path(s)).string());
    };
    std::vector<std::string> searchPaths;
    searchPaths.push_back(entryDir);
    addIfNonEmpty(searchPaths, getEnv("CAMEL_PACKAGES"));
    addIfNonEmpty(searchPaths, getEnv("CAMEL_STD_LIB"));
    addIfNonEmpty(searchPaths, (camelPath / "stdlib").string());

    g_state.ctx = Context::create(
        EntryConfig{
            .entryDir    = entryDir,
            .entryFile   = targetFile,
            .searchPaths = std::move(searchPaths),
        },
        DiagsConfig{
            .total_limit         = -1,
            .per_severity_limits = {{Severity::Error, 0}},
        });

    g_state.parser   = make_shared<CamelParser>(diagnostics);
    g_state.mainModule = make_shared<UserDefinedModule>("main", targetFile, g_state.ctx, g_state.parser);
    g_state.ctx->setMainModule(g_state.mainModule);

    // Initialize memory management
    (void)mm::autoSpace();
    (void)mm::metaSpace();
    (void)mm::permSpace();

    // Start memory monitor: periodic GC region scan, HTTP server + Web UI
    if (!g_memMonitor.isRunning()) {
        g_memMonitor.start(8765);
    }

    try {
        g_state.parser->parse(*file);
        g_state.mainModule->compile(CompileStage::Done);

        if (!g_state.mainModule->loaded()) {
            g_state.ctx->dumpAllModuleDiagnostics(cout, false);
            return false;
        }

        std::vector<std::string> passes;
        int retCode = applyPasses(passes, g_state.ctx, cout);
        if (retCode != 0) {
            const auto &diags = g_state.ctx->rtmDiags();
            if (diags->hasErrors()) {
                diags->dump(cout, false);
            }
            return false;
        }
        cout << "Run completed." << endl;
        return true;
    } catch (Diagnostic &d) {
        cout << "Diagnostic error: " << d.toText() << endl;
        return false;
    } catch (exception &e) {
        cout << "Error: " << e.what() << endl;
        return false;
    }
}

// REPL main loop
void repl() {
    string prompt = "(camel-db) ";

    printVersion();
    cout << "Type 'help' to see available commands" << endl << endl;

    while (true) {
        cout << prompt << flush;

        string line;
        if (!getline(cin, line)) {
            if (cin.eof()) {
                cout << "\n(EOF detected on stdin, exiting)" << endl;
            }
            break;
        }

        auto [cmd, arg] = parseCommand(line);

        if (cmd.empty())
            continue;

        // Command aliases
        if (cmd == "f" || cmd == "file") {
            cmdFile(arg);
        } else if (cmd == "r" || cmd == "run") {
            cmdRun();
        } else if (cmd == "h" || cmd == "help" || cmd == "?") {
            printHelp();
        } else if (cmd == "v" || cmd == "version") {
            printVersion();
        } else if (cmd == "q" || cmd == "quit" || cmd == "exit") {
            if (g_memMonitor.isRunning()) {
                g_memMonitor.stop();
            }
            cout << "Goodbye." << endl;
            break;
        } else {
            cout << "Unknown command: '" << cmd << "'. Type 'help' for help." << endl;
        }
    }
}

int main(int argc, char *argv[]) {
    // 1) exe dir  2) ../libs  3) system default
    camel::utils::setupLibrarySearchPath();

    // Disable stdout buffering so output is visible immediately (e.g. in PowerShell)
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // Optional: camel-dbg [file.cml] to load file directly
    if (argc >= 2) {
        string arg = argv[1];
        if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            printVersion();
            return 0;
        }
        g_state.targetFile = arg;
    }

    repl();
    return 0;
}
