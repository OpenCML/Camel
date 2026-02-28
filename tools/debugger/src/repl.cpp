/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Updated: Feb. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * @file repl.cpp
 * @brief 交互式 REPL：解析用户输入、映射到内部命令 ID、构造 JSON 参数后交给 CommandDispatcher，与
 * Web UI 共享同一套命令语义。
 *
 * 设计要点：
 * - 命令名采用“REPL 简写 + 内部 DAP 风格 ID”的映射（kAliases），便于用户输入简短命令同时后端统一用
 * loadSource/launch 等 ID。
 * - 启动时通过 printStartupBanner 根据 StartupOptions 派发
 * startServer/loadSource/configure/launch，使初始化也走命令模式并回显，状态可追溯。
 */

#include "windows_parser_guard.h"

// nlohmann MUST be before any header that pulls in ANTLR (EOF conflict)
#include "nlohmann/json.hpp"

#include "build_config.h"
#include "command/dispatcher.h"
#include "repl.h"
#include "state.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>

namespace debugger {

using json = nlohmann::json;

static const char *const VERSION = "0.1.0";

static std::string trim(const std::string &s) {
    size_t start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos)
        return "";
    size_t end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

static std::pair<std::string, std::string> parseCommand(const std::string &line) {
    std::string t = trim(line);
    if (t.empty())
        return {"", ""};
    size_t space = t.find(' ');
    if (space == std::string::npos)
        return {t, ""};
    return {trim(t.substr(0, space)), trim(t.substr(space + 1))};
}

/// REPL 输入到内部命令 ID 的映射：简短别名（如 s/f/r）与完整名（serve/file/run）均映射到同一 ID，与
/// HTTP 派发使用的 ID 一致。
static const std::unordered_map<std::string, std::string> kAliases = {
    {"f", "loadSource"},
    {"file", "loadSource"},
    {"r", "launch"},
    {"run", "launch"},
    {"c", "continue"},
    {"continue", "continue"},
    {"s", "startServer"},
    {"serve", "startServer"},
    {"verbose", "configure"},
    {"logfile", "configure"},
    {"q", "disconnect"},
    {"quit", "disconnect"},
    {"exit", "disconnect"},
    {"restart", "restart"},
    {"terminate", "terminate"},
    {"breakpoint-filter", "setBreakpointFilter"},
    {"alloc-breakpoint-spaces", "setBreakpointFilter"},
    {"breakpoints", "breakpointStatus"},
    {"breakpoint-status", "breakpointStatus"},
    {"breakpoint-types", "setBreakpointTypes"},
};

static std::string escapeJsonString(const std::string &s) {
    std::string out;
    for (char c : s) {
        if (c == '\\' || c == '"')
            out += '\\';
        out += c;
    }
    return out;
}

/// 根据命令 ID 与 REPL 参数构造 JSON body，与 HTTP POST body 格式一致，便于 dispatcher
/// 复用同一套解析逻辑。
static std::string
buildArgsJson(const std::string &commandName, const std::string &replCmd, const std::string &arg) {
    if (commandName == "loadSource")
        return R"({"path":")" + escapeJsonString(arg) + R"("})";

    if (commandName == "launch")
        return "{}";

    if (commandName == "startServer") {
        int port = 8765;
        if (!arg.empty())
            port = std::atoi(arg.c_str());
        if (port <= 0)
            port = 8765;
        return R"({"port":)" + std::to_string(port) + "}";
    }

    if (commandName == "configure") {
        if (replCmd == "verbose") {
            std::string v = trim(arg);
            bool on       = (v.empty() || v == "on" || v == "1");
            return on ? R"({"verbose":true})" : R"({"verbose":false})";
        }
        if (replCmd == "logfile") {
            if (arg.empty() || arg == "off" || arg == "0")
                return R"({"logFile":""})";
            return R"({"logFile":")" + escapeJsonString(arg) + R"("})";
        }
    }

    if (commandName == "setBreakpointFilter") {
        // breakpoint-filter / alloc-breakpoint-spaces（统一断点模型，按空间类型）
        std::string list = trim(arg);
        if (list.empty())
            return R"({"breakSpaces":[]})";
        std::string out = R"({"breakSpaces":[)";
        bool first      = true;
        for (size_t i = 0; i < list.size();) {
            while (i < list.size() && (list[i] == ' ' || list[i] == '\t'))
                ++i;
            if (i >= list.size())
                break;
            size_t j = i;
            while (j < list.size() && list[j] != ' ' && list[j] != '\t')
                ++j;
            std::string s = list.substr(i, j - i);
            if (!s.empty()) {
                if (!first)
                    out += ',';
                out += '"' + s + '"';
                first = false;
            }
            i = j;
        }
        out += "]}";
        return out;
    }

    return "{}";
}

/// 对“按任务”命令，若 argsJson 中无 target 且当前有前台任务，则注入 target 便于命令层转发。
static std::string
injectForegroundTarget(const std::string &argsJson, const std::string &commandName) {
    static const std::unordered_map<std::string, bool> kTaskCommands = {
        {"continue", true},
        {"restart", true},
        {"terminate", true},
        {"configure", true},
        {"setBreakpointFilter", true},
        {"setBreakpointTypes", true},
    };
    if (kTaskCommands.count(commandName) == 0)
        return argsJson;
    std::string fg = getForegroundTaskId();
    if (fg.empty())
        return argsJson;
    try {
        json j = argsJson.empty() ? json::object() : json::parse(argsJson, nullptr, false);
        if (!j.is_object())
            return argsJson;
        if (j.contains("target") && !j["target"].get<std::string>().empty())
            return argsJson;
        j["target"] = fg;
        return j.dump();
    } catch (...) {
        return argsJson;
    }
}

void printHelp() {
    std::cout << R"(Camel Debugger - Interactive Camel debugger

Usage:
  camel-db [options] [file.cml]

Options (startup):
  -s, --serve [port]   Start API server on port (default 8765) before REPL
  -f, --file <path>    Load file to debug (same as positional file.cml)
  -r, --run            Run loaded file once after startup
  -V, --verbose        Enable verbose output from startup
  --logfile <path>     Write program output to file from startup
  --run-worker <path>  Run one script and exit (for multi-process child; no server/REPL)
  -h, --help           Show this help
  -v, --version        Show version

REPL commands (after startup):
  serve [port]  (s)  Start API server (default 8765)
  file <path>   (f)  Load Camel source file to debug
  run          (r)  Run the loaded file (options configurable in Web UI)
  continue     (c)  Resume paused execution
  restart           Restart execution
  terminate         Terminate execution
  task [id]      (fg)  Show or set foreground task (default target for continue/restart/terminate/etc.)
  task list           List all tasks (id, port, state, path)
  task state [id]     Show detailed state of foreground task or specified task by id
  verbose [on|off]     Enable or disable verbose output
  logfile [path]       Write program output to file (path=off to close)
  breakpoint-filter [space1 ...]   Set breakpoint filter (alloc spaces; empty = none)
  alloc-breakpoint-spaces [space1 ...]  Same as breakpoint-filter
  breakpoints          Show breakpoint status by type (alloc spaces, etc.)
  breakpoint-types     Set enabled breakpoint types
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

void printVersion() {
#ifdef NDEBUG
    const char *buildType = "Build";
#else
    const char *buildType = "Debug";
#endif
    std::cout << "Camel Debugger v" << VERSION << " (" << buildType << " " << BUILD_FOOTPRINT << ")"
              << std::endl;
}

/// 根据 StartupOptions 派发 startServer/loadSource/configure/launch，使命令行参数与 REPL
/// 输入走同一套命令并回显，避免“静默”初始化导致状态不可见。
void printStartupBanner() {
    auto &dispatcher = getDispatcher();
    auto &opts       = getStartupOptions();

    printVersion();
    std::cout << std::endl;

    if (opts.serve)
        dispatcher.dispatch("startServer", R"({"port":)" + std::to_string(opts.port) + "}");
    if (!opts.file.empty())
        dispatcher.dispatch("loadSource", R"({"path":")" + escapeJsonString(opts.file) + R"("})");
    if (opts.verbose || !opts.logFile.empty()) {
        std::string cfgArgs = "{";
        if (opts.verbose)
            cfgArgs += R"("verbose":true)";
        if (!opts.logFile.empty()) {
            if (opts.verbose)
                cfgArgs += ",";
            cfgArgs += R"("logFile":")" + escapeJsonString(opts.logFile) + R"(")";
        }
        cfgArgs += "}";
        dispatcher.dispatch("configure", cfgArgs);
    }
    if (opts.run && getState().hasFile())
        dispatcher.dispatch("launch", "{}");

    std::cout << std::endl;
    if (getServer().isRunning())
        std::cout << "Type 'help' for commands." << std::endl;
    else
        std::cout << "Type 'help' for commands. Use 'serve' to start the API server." << std::endl;
    std::cout << std::endl;
}

void repl() {
    std::string prompt = "(camel-db) ";
    auto &dispatcher   = getDispatcher();
    printStartupBanner();

    while (true) {
        std::cout << prompt << std::flush;
        std::string line;
        if (!std::getline(std::cin, line)) {
            // getline 失败：可能是 Ctrl+C（SIGINT 已设 isInterrupted）、stdin
            // 关闭或读错误；统一在此处理退出或保持 API 运行。
            if (isInterrupted()) {
                std::cout << std::endl;
                if (getServer().isRunning())
                    getServer().stop();
            } else if (std::cin.eof()) {
                if (getServer().isRunning()) {
                    std::cout << "\n(stdin closed; debugger keeps running as API service)"
                              << std::endl;
                    while (getServer().isRunning())
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    std::cout << "API stopped. Exiting." << std::endl;
                } else {
                    std::cout << "\n(stdin closed.) To keep the debugger running, start the API "
                                 "server first: "
                              << "run with -s (e.g. camel-db -s hello.cml) or type 'serve' before "
                                 "'run'."
                              << std::endl;
                }
            } else {
                if (getServer().isRunning()) {
                    std::cout << "\n(stdin error; debugger keeps running as API service)"
                              << std::endl;
                    while (getServer().isRunning())
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    std::cout << "API stopped. Exiting." << std::endl;
                }
            }
            break;
        }

        auto [cmd, arg] = parseCommand(line);
        if (cmd.empty())
            continue;

        if (cmd == "h" || cmd == "help" || cmd == "?") {
            printHelp();
            continue;
        }
        if (cmd == "v" || cmd == "version") {
            printVersion();
            continue;
        }
        if (cmd == "task" || cmd == "fg") {
            if (arg.empty()) {
                std::string fg = getForegroundTaskId();
                if (fg.empty())
                    std::cout << "No foreground task set." << std::endl;
                else
                    std::cout << "Foreground task: " << fg << std::endl;
            } else if (arg == "list") {
                std::string argsJson = R"({"subcommand":"list"})";
                dispatcher.dispatch("task", argsJson);
            } else if (arg.size() >= 5 && arg.substr(0, 5) == "state") {
                std::string rest = trim(arg.substr(5));
                json j;
                j["subcommand"] = "state";
                if (!rest.empty())
                    j["target"] = rest;
                dispatcher.dispatch("task", j.dump());
            } else {
                setForegroundTaskId(arg);
                std::cout << "Foreground task: " << arg << std::endl;
            }
            continue;
        }

        auto it = kAliases.find(cmd);
        if (it != kAliases.end()) {
            std::string argsJson = buildArgsJson(
                it->second,
                cmd,
                arg); // 同一命令 ID 下 REPL 参数转 JSON，与 HTTP body 一致
            argsJson    = injectForegroundTarget(argsJson, it->second);
            auto result = dispatcher.dispatch(it->second, argsJson);
            if (it->second == "disconnect")
                break;
        } else {
            std::cout << "Unknown command: '" << cmd << "'. Type 'help' for help." << std::endl;
        }
    }
}

} // namespace debugger
