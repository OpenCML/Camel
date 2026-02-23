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
 * Created: Feb. 22, 2026
 * Updated: Feb. 24, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "build_config.h"
#include "debugger_server.h"

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics.h"
#include "camel/core/error/listener.h"
#include "camel/core/mm.h"
#ifndef NDEBUG
#include "camel/core/debug_breakpoint.h"
#include "camel/core/mm/debug_hook.h"
#endif
#include "camel/core/module/userdef.h"
#include "camel/core/type.h"
#include "camel/execute/pass/base.h"
#include "camel/parse/antlr/OpenCMLLexer.h"
#include "camel/parse/antlr/OpenCMLParser.h"
#include "camel/parse/parse.h"
#include "camel/utils/assert.h"
#include "camel/utils/dll_path.h"
#include "camel/utils/env.h"
#include "camel/utils/log.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
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
static debugger::DebuggerServer g_server;
static bool g_verbose = false;
static std::string g_logFilePath;
static size_t g_logStreamHandle = 0;
static std::shared_ptr<std::ofstream> g_logFileStream;

static void setLogFilePath(const std::string &path) {
    if (g_logStreamHandle != 0) {
        Logger::RemoveOutputStream(g_logStreamHandle);
        g_logStreamHandle = 0;
        g_logFileStream.reset();
    }
    g_logFilePath = path;
    if (!path.empty()) {
        g_logFileStream = std::make_shared<std::ofstream>(path, std::ios::app);
        if (g_logFileStream->is_open())
            g_logStreamHandle = Logger::AddOutputStream(g_logFileStream.get());
    }
}

// Print help
void printHelp() {
    cout << R"(Camel Debugger - Interactive Camel debugger

Usage:
  camel-db [options] [file.cml]

Options (startup):
  -s, --serve [port]   Start API server on port (default 8765) before REPL
  -f, --file <path>    Load file to debug (same as positional file.cml)
  -r, --run            Run loaded file once after startup (memory monitor on, alloc-step off)
  -V, --verbose        Enable verbose output from startup
  --logfile <path>     Write program output to file from startup
  -h, --help           Show this help
  -v, --version        Show version

REPL commands (after startup):
  serve [port]  (s)  Start API server (default 8765)
  file <path>   (f)  Load Camel source file to debug
  run          (r)  Run the loaded file (options configurable in Web UI)
  verbose [on|off]     Enable or disable verbose output
  logfile [path]       Write program output to file (path=off to close)
  help         (h)  Show this help
  version      (v)  Show version info
  quit         (q)  Exit debugger

Examples:
  camel-db --serve
  camel-db --serve 9000 hello.cml
  camel-db hello.cml -s -r
  camel-db -f script.cml --serve --logfile debug.log
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

// JSON 转义并返回 /api/state 的 JSON 字符串（含 settings 如 verbose）
static string getStateJson() {
    ostringstream oss;
    oss << "{\"serverRunning\":" << (g_server.isRunning() ? "true" : "false")
        << ",\"memoryMonitorRunning\":" << (g_server.isMemoryScanRunning() ? "true" : "false")
        << ",\"allocStepEnabled\":" << (g_server.allocStepEnabled() ? "true" : "false")
        << ",\"hasFile\":" << (g_state.hasFile() ? "true" : "false")
        << ",\"verbose\":" << (g_verbose ? "true" : "false");
    if (g_state.hasFile()) {
        oss << ",\"targetFile\":\"";
        for (char c : g_state.targetFile) {
            if (c == '\\' || c == '"')
                oss << '\\';
            oss << c;
        }
        oss << "\"";
    } else {
        oss << ",\"targetFile\":\"\"";
    }
    oss << "}";
    return oss.str();
}

// True if s looks like a positive integer (for port)
static bool isPositiveInt(const string &s) {
    if (s.empty())
        return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

// Parse command: returns (cmd, arg)
static pair<string, string> parseCommand(const string &line) {
    string trimmed = trim(line);
    if (trimmed.empty())
        return {"", ""};

    size_t space = trimmed.find(' ');
    if (space == string::npos)
        return {trimmed, ""};

    string cmd = trim(trimmed.substr(0, space));
    string arg = trim(trimmed.substr(space + 1));
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
    std::string msg    = "Loaded: " + path;
    cout << msg << endl;
    Logger::WriteToAllStreams(msg);
    return true;
}

// Execute run command: run loaded file (options from CLI default: memoryMonitor=true,
// allocStep=NDEBUG?false) Supports restart from Web UI when paused at alloc step: cleans state and
// re-runs from scratch.
bool cmdRun(bool enableMemoryMonitor = true, bool enableAllocStep = false) {
    if (!g_state.hasFile()) {
        cout << "Error: please load a file first with file <path>" << endl;
        return false;
    }

    const string &targetFile = g_state.targetFile;

    for (;;) {
        auto file = make_unique<std::ifstream>(targetFile);
        if (!file->is_open()) {
            cout << "Error: cannot open file " << targetFile << endl;
            return false;
        }

        // 在 Context::create 等任何可能使用 perm/alloc 的代码之前启用，否则早期分配不会触发断点且
        // snapshot 不完整
        if (g_server.isRunning()) {
            if (enableMemoryMonitor)
                g_server.startMemoryScan();
            if (enableAllocStep) {
                g_server.enableAllocStep(true);
#ifndef NDEBUG
                camel::DebugBreakpoint::EnableType("alloc_before");
                camel::DebugBreakpoint::EnableType("alloc");
#endif
            } else {
                g_server.enableAllocStep(false);
#ifndef NDEBUG
                camel::DebugBreakpoint::DisableType("alloc_before");
                camel::DebugBreakpoint::DisableType("alloc");
#endif
            }
        }
        std::string runMsg = "Running " + targetFile +
                             " (memory monitor=" + (enableMemoryMonitor ? "on" : "off") +
                             ", alloc step=" + (enableAllocStep ? "on" : "off") + ")";
        cout << runMsg << endl;
        Logger::WriteToAllStreams(runMsg);

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

        g_state.parser = make_shared<CamelParser>(diagnostics);
        g_state.mainModule =
            make_shared<UserDefinedModule>("main", targetFile, g_state.ctx, g_state.parser);
        g_state.ctx->setMainModule(g_state.mainModule);

        (void)mm::autoSpace();
        (void)mm::metaSpace();
        (void)mm::permSpace();

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
        } catch (debugger::RestartRequestedException &) {
            cout << "Restarting script..." << endl;
            continue;
        } catch (Diagnostic &d) {
            cout << "Diagnostic error: " << d.toText() << endl;
            return false;
        } catch (exception &e) {
            cout << "Error: " << e.what() << endl;
            return false;
        }
    }
}

// REPL main loop
void repl() {
    string prompt = "(camel-db) ";

    printVersion();
    cout << "Type 'help' to see available commands. Use 'serve' to start the API server." << endl
         << endl;
    if (!g_state.targetFile.empty())
        cout << "Command line: file = " << g_state.targetFile << endl;
    if (g_verbose)
        cout << "Command line: verbose = on" << endl;
    if (!g_state.targetFile.empty() || g_verbose)
        cout << endl;

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
        if (cmd == "s" || cmd == "serve") {
            int port = 8765;
            if (!arg.empty())
                port = atoi(arg.c_str());
            if (port <= 0)
                port = 8765;
            if (!g_server.isRunning()) {
                g_server.start(port);
            } else {
                std::string m = "API already on port " + std::to_string(g_server.port());
                cout << m << endl;
                Logger::WriteToAllStreams(m);
            }
        } else if (cmd == "f" || cmd == "file") {
            cmdFile(arg);
        } else if (cmd == "r" || cmd == "run") {
            cmdRun(true, false); // CLI 默认开启内存监控，不开启 alloc-step
        } else if (cmd == "h" || cmd == "help" || cmd == "?") {
            printHelp();
        } else if (cmd == "verbose") {
            string v = trim(arg);
            if (v.empty() || v == "on" || v == "1") {
                g_verbose = true;
                Logger::SetVerbose(true);
                cout << "Verbose on." << endl;
            } else if (v == "off" || v == "0") {
                g_verbose = false;
                Logger::SetVerbose(false);
                cout << "Verbose off." << endl;
            } else {
                cout << "Usage: verbose [on|off]. Current: " << (g_verbose ? "on" : "off") << endl;
            }
        } else if (cmd == "v" || cmd == "version") {
            printVersion();
        } else if (cmd == "logfile") {
            if (arg.empty() || arg == "off" || arg == "0") {
                setLogFilePath("");
                cout << "Log file: off" << endl;
            } else {
                setLogFilePath(arg);
                cout << "Log file: " << arg << endl;
            }
        } else if (cmd == "q" || cmd == "quit" || cmd == "exit") {
            if (g_server.isRunning()) {
#ifndef NDEBUG
                mm::clearPostAllocDebugHook();
#endif
                g_server.stop();
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

    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

#ifndef NDEBUG
    // 让 libcamel 的 ASSERT 抛出异常而非 abort，便于 debugger 捕获并由用户选择重启
    cml::set_assert_handler([](const std::string &expression,
                               const std::string &suggestion,
                               const std::source_location &location) {
        throw cml::AssertionFailure(expression, suggestion, location);
    });
#endif

    // 注入 API 回调：state / file / run / settings 由 REPL 逻辑提供
    g_server.setDebuggerCallbacks(
        getStateJson,
        cmdFile,
        [](bool memoryMonitor, bool allocStep) { return cmdRun(memoryMonitor, allocStep); },
        [](bool verbose, const std::string &logFile) {
            g_verbose = verbose;
            Logger::SetVerbose(verbose);
            setLogFilePath(logFile);
            cout << "Settings: verbose " << (verbose ? "on" : "off") << endl;
        });
#ifndef NDEBUG
    camel::DebugBreakpoint::RegisterType("alloc");
    camel::DebugBreakpoint::RegisterType("alloc_before");
    camel::DebugBreakpoint::SetHandler([](const char *type, const void *ctx) {
        if (!type)
            return;
        if (std::strcmp(type, "alloc_before") == 0) {
            auto *evt = static_cast<const mm::PreAllocEvent *>(ctx);
            if (evt)
                g_server.pauseAndWaitForContinue(nullptr, evt->size, evt->space);
            return;
        }
        if (std::strcmp(type, "alloc") == 0) {
            auto *evt = static_cast<const mm::AllocEvent *>(ctx);
            if (evt)
                g_server.pauseAndWaitForContinue(evt->ptr, evt->size, evt->space);
        }
    });
#endif

    bool doServe  = false;
    int servePort = 8765;
    string cliFile;
    bool doRun = false;
    string logFilePath;

    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            printHelp();
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            printVersion();
            return 0;
        }
        if (arg == "-V" || arg == "--verbose") {
            g_verbose = true;
        } else if (arg == "-s" || arg == "--serve") {
            doServe = true;
            if (i + 1 < argc && isPositiveInt(argv[i + 1])) {
                servePort = std::atoi(argv[i + 1]);
                ++i;
            }
        } else if (arg == "-f" || arg == "--file") {
            if (i + 1 < argc)
                cliFile = argv[++i];
        } else if (arg == "-r" || arg == "--run") {
            doRun = true;
        } else if (arg == "--logfile") {
            if (i + 1 < argc)
                logFilePath = argv[++i];
        } else if (!arg.empty() && arg[0] != '-') {
            if (cliFile.empty())
                cliFile = arg;
        }
    }

    if (g_verbose)
        Logger::SetVerbose(true);
#ifndef NDEBUG
    if (!doServe)
        Logger::AddOutputStream(&std::cout);
#endif
    if (!logFilePath.empty())
        setLogFilePath(logFilePath);
    if (doServe)
        g_server.start(servePort);
    if (!cliFile.empty())
        cmdFile(cliFile);
    if (doRun && g_state.hasFile())
        cmdRun(true, false);

    repl();
    return 0;
}
