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
 * Created: Feb. 25, 2026
 * Updated: Feb. 26, 2026
 * Supported by: National Key Research and Development Program of China
 */

// nlohmann MUST be included before any header that pulls in ANTLR (EOF conflict)
#include "nlohmann/json.hpp"

#include "command.h"
#include "commands.h"
#include "dispatcher.h"

#include "camel/core/debug_breakpoint.h"
#include "spawn.h"
#include "state.h"

#ifndef NDEBUG
#include "camel/core/mm/debug_hook.h"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

namespace fs = std::filesystem;

namespace debugger {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// loadSource — load a .cml source file
// ---------------------------------------------------------------------------
class LoadSourceCommand final : public Command {
  public:
    const char *name() const override { return "loadSource"; }
    const char *description() const override { return "Load a Camel source file"; }

    CommandResult execute(const std::string &argsJson) override {
        json args        = json::parse(argsJson, nullptr, false);
        std::string path = args.value("path", "");

        std::string display = "file " + (path.empty() ? "<path>" : path);
        if (path.empty())
            return CommandResult::error(display, "Usage: file <path>");
        if (!fs::exists(path))
            return CommandResult::error(display, "Error: file not found: " + path);

        getState().targetFile = path;
        getTaskState()        = "loaded";
        return CommandResult::ok(display, "Loaded: " + path);
    }
};

// ---------------------------------------------------------------------------
// launch — spawn a worker process to run the loaded script
// ---------------------------------------------------------------------------
class LaunchCommand final : public Command {
  public:
    const char *name() const override { return "launch"; }
    const char *description() const override { return "Run the loaded script in a worker process"; }

    CommandResult execute(const std::string &argsJson) override {
        // launch 只在父进程中有意义（spawn 子进程）；子进程不应再 spawn，由 WorkerRunHandler 处理
        // /api/run。
        if (getServer().isWorkerProcess())
            return CommandResult::error("run", "Error: launch is not available in worker process.");

        json args          = json::parse(argsJson, nullptr, false);
        bool memoryMonitor = args.value("memoryMonitor", true);
        bool allocStep     = args.value("allocStep", false);

        std::unordered_set<std::string> breakSpaces;
        if (args.contains("breakSpaces") && args["breakSpaces"].is_array())
            for (const auto &v : args["breakSpaces"])
                if (v.is_string() && !v.get<std::string>().empty())
                    breakSpaces.insert(v.get<std::string>());

        std::string path = args.value("path", "");
        if (path.empty())
            path = getState().targetFile;

        std::string display = "run (memory monitor=" + std::string(memoryMonitor ? "on" : "off") +
                              ", alloc step=" + std::string(allocStep ? "on" : "off") + ")";

        if (path.empty() || !getState().hasFile())
            return CommandResult::error(
                display,
                "Error: please load a file first with file <path>");

        getServer().setAllocBreakSpaces(breakSpaces);

        auto [ok, workerPort] = spawnWorker(path, memoryMonitor, allocStep, breakSpaces);
        if (ok && workerPort > 0) {
            getServer().setChildPort(workerPort);
            for (int retry = 0; retry < 15; ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (getServer().forwardPostToChild("/api/run", argsJson))
                    break;
            }
        }

        if (ok)
            return CommandResult::ok(
                display,
                "Run started (worker port " + std::to_string(workerPort) + ").");
        return CommandResult::error(display, "Run failed to start.");
    }
};

// ---------------------------------------------------------------------------
// startServer — start the HTTP API server
// ---------------------------------------------------------------------------
class StartServerCommand final : public Command {
  public:
    const char *name() const override { return "startServer"; }
    const char *description() const override { return "Start the HTTP API server"; }

    CommandResult execute(const std::string &argsJson) override {
        json args = json::parse(argsJson, nullptr, false);
        int port  = args.value("port", 8765);
        if (port <= 0)
            port = 8765;

        std::string display = "serve " + std::to_string(port);

        if (getServer().isRunning())
            return CommandResult::ok(
                display,
                "API already on port " + std::to_string(getServer().port()));

        getServer().start(port, true);
        return CommandResult::ok(
            display,
            "API http://127.0.0.1:" + std::to_string(port) +
                " | Web UI: python tools/debugger/serve_ui.py\n"
                "API server started on port " +
                std::to_string(port) + ". Debugger will keep running even if stdin is closed.");
    }
};

// ---------------------------------------------------------------------------
// continue — resume paused execution
// ---------------------------------------------------------------------------
class ContinueCommand final : public Command {
  public:
    const char *name() const override { return "continue"; }
    const char *description() const override { return "Resume paused execution"; }

    CommandResult execute(const std::string &) override {
        auto &srv = getServer();
        // 父进程：转发到子进程；子进程：直接本地执行，跳过无意义的 forwardPostToChild 尝试。
        if (!srv.isWorkerProcess() && srv.forwardPostToChild("/api/continue"))
            return CommandResult::ok("continue", "Continue (forwarded to worker).");
        srv.requestContinue();
        return CommandResult::ok("continue", "Continue.");
    }
};

// ---------------------------------------------------------------------------
// restart — 父进程：杀掉目标任务子进程并重新 spawn，实现解释器状态彻底刷新；子进程：原地
// requestRestart
// ---------------------------------------------------------------------------
class RestartCommand final : public Command {
  public:
    const char *name() const override { return "restart"; }
    const char *description() const override {
        return "Restart execution (kill worker and respawn for full refresh)";
    }

    CommandResult execute(const std::string &argsJson) override {
        auto &srv = getServer();
        if (srv.isWorkerProcess()) {
            srv.clearLastRunError();
            srv.requestRestart();
            return CommandResult::ok("restart", "Restart.");
        }
        // 父进程：按 target 终止对应 worker 并重新 spawn
        json args          = json::parse(argsJson, nullptr, false);
        std::string target = args.value("target", "");
        int port           = resolveTargetToPort(target);
        if (port <= 0)
            return CommandResult::error(
                "restart",
                "No task selected or target invalid. Select a task in the sidebar or pass target.");
        std::vector<TaskInfo> tasks = getTasks();
        std::string scriptPath;
        for (const auto &t : tasks)
            if (t.port == port) {
                scriptPath = t.scriptPath;
                break;
            }
        if (scriptPath.empty())
            return CommandResult::error("restart", "Task not found or already exited.");
        if (!terminateWorker(port))
            return CommandResult::error("restart", "Failed to terminate worker.");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        bool memoryMonitor = args.value("memoryMonitor", true);
        bool allocStep     = args.value("allocStep", false);
        std::unordered_set<std::string> breakSpaces;
        if (args.contains("breakSpaces") && args["breakSpaces"].is_array())
            for (const auto &v : args["breakSpaces"])
                if (v.is_string() && !v.get<std::string>().empty())
                    breakSpaces.insert(v.get<std::string>());
        auto [ok, newPort] = spawnWorker(scriptPath, memoryMonitor, allocStep, breakSpaces);
        if (!ok || newPort <= 0)
            return CommandResult::error("restart", "Respawn failed.");
        srv.setChildPort(newPort);
        json runBody;
        runBody["memoryMonitor"] = memoryMonitor;
        runBody["allocStep"]     = allocStep;
        runBody["breakSpaces"]   = args.value("breakSpaces", json::array());
        runBody["breakStages"]   = args.value("breakStages", json::array());
        for (int retry = 0; retry < 15; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (srv.forwardPostToChild("/api/run", runBody.dump()))
                break;
        }
        return CommandResult::ok(
            "restart",
            "Restart (worker " + std::to_string(port) + " terminated, new worker " +
                std::to_string(newPort) + ").");
    }
};

// ---------------------------------------------------------------------------
// terminate — terminate execution
// ---------------------------------------------------------------------------
class TerminateCommand final : public Command {
  public:
    const char *name() const override { return "terminate"; }
    const char *description() const override { return "Terminate execution"; }

    CommandResult execute(const std::string &) override {
        auto &srv = getServer();
        if (!srv.isWorkerProcess() && srv.forwardPostToChild("/api/terminate"))
            return CommandResult::ok("terminate", "Terminate (forwarded to worker).");
        srv.requestTerminate();
        return CommandResult::ok("terminate", "Terminate requested.");
    }
};

// ---------------------------------------------------------------------------
// configure — set verbose / logFile
// ---------------------------------------------------------------------------
class ConfigureCommand final : public Command {
  public:
    const char *name() const override { return "configure"; }
    const char *description() const override { return "Set verbose mode and/or log file"; }

    CommandResult execute(const std::string &argsJson) override {
        json args           = json::parse(argsJson, nullptr, false);
        bool verbose        = args.value("verbose", getVerbose());
        std::string logFile = args.value("logFile", "");
        bool hasLogFile     = args.contains("logFile");

        setSettings(verbose, hasLogFile ? logFile : "");

        std::string display = "verbose " + std::string(verbose ? "on" : "off");
        std::string msg     = "Settings: verbose " + std::string(verbose ? "on" : "off");
        if (hasLogFile) {
            display += ", logfile " + (logFile.empty() ? "off" : logFile);
            msg += "\nLog file: " + (logFile.empty() ? "off" : logFile);
        }
        return CommandResult::ok(display, msg);
    }
};

// ---------------------------------------------------------------------------
// setBreakpointFilter — set breakpoint space filters
// ---------------------------------------------------------------------------
class SetBreakpointFilterCommand final : public Command {
  public:
    const char *name() const override { return "setBreakpointFilter"; }
    const char *description() const override { return "Set alloc breakpoint space filters"; }

    CommandResult execute(const std::string &argsJson) override {
        json args = json::parse(argsJson, nullptr, false);
        std::unordered_set<std::string> breakSpaces;
        if (args.contains("breakSpaces") && args["breakSpaces"].is_array())
            for (const auto &v : args["breakSpaces"])
                if (v.is_string() && !v.get<std::string>().empty())
                    breakSpaces.insert(v.get<std::string>());

        std::string display = "breakpoint-filter";
        std::string msg;
        if (breakSpaces.empty()) {
            msg = "Breakpoint spaces: (all)";
        } else {
            msg        = "Breakpoint spaces: ";
            bool first = true;
            for (const auto &b : breakSpaces) {
                if (!first)
                    msg += ", ";
                msg += b;
                first = false;
            }
        }

        getServer().setAllocBreakSpaces(std::move(breakSpaces));
        return CommandResult::ok(display, msg);
    }
};

// ---------------------------------------------------------------------------
// setBreakpointTypes — enable/disable breakpoint types
// ---------------------------------------------------------------------------
class SetBreakpointTypesCommand final : public Command {
  public:
    const char *name() const override { return "setBreakpointTypes"; }
    const char *description() const override { return "Enable/disable breakpoint types"; }

    CommandResult execute(const std::string &argsJson) override {
        json args    = json::parse(argsJson, nullptr, false);
        auto enabled = args.value("enabled", std::vector<std::string>{});
        auto known   = camel::DebugBreakpoint::GetKnownTypes();
        std::unordered_set<std::string> toEnable(enabled.begin(), enabled.end());

        for (const auto &t : known) {
            if (t == "alloc_before")
                continue;
            if (toEnable.count(t)) {
                camel::DebugBreakpoint::EnableType(t.c_str());
                if (t == "alloc")
                    camel::DebugBreakpoint::EnableType("alloc_before");
            } else {
                camel::DebugBreakpoint::DisableType(t.c_str());
                if (t == "alloc")
                    camel::DebugBreakpoint::DisableType("alloc_before");
            }
        }
        for (const auto &t : enabled)
            if (std::find(known.begin(), known.end(), t) == known.end() && t != "alloc_before")
                camel::DebugBreakpoint::EnableType(t.c_str());

        getServer().enableAllocStep(toEnable.count("alloc") > 0);

        std::string msg = "Breakpoint types: ";
        if (enabled.empty()) {
            msg += "(none)";
        } else {
            bool first = true;
            for (const auto &t : enabled) {
                if (!first)
                    msg += ", ";
                msg += t;
                first = false;
            }
        }
        return CommandResult::ok("breakpoint-types", msg);
    }
};

// ---------------------------------------------------------------------------
// disconnect — quit the debugger
// ---------------------------------------------------------------------------
class DisconnectCommand final : public Command {
  public:
    const char *name() const override { return "disconnect"; }
    const char *description() const override { return "Quit the debugger"; }

    CommandResult execute(const std::string &) override {
        auto &srv = getServer();
        if (srv.isRunning()) {
#ifndef NDEBUG
            mm::clearPostAllocDebugHook();
#endif
            srv.stop();
        }
        return CommandResult::ok("quit", "Goodbye.");
    }
};

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------
void registerAllCommands(CommandDispatcher &dispatcher) {
    dispatcher.registerCommand(std::make_shared<LoadSourceCommand>());
    dispatcher.registerCommand(std::make_shared<LaunchCommand>());
    dispatcher.registerCommand(std::make_shared<StartServerCommand>());
    dispatcher.registerCommand(std::make_shared<ContinueCommand>());
    dispatcher.registerCommand(std::make_shared<RestartCommand>());
    dispatcher.registerCommand(std::make_shared<TerminateCommand>());
    dispatcher.registerCommand(std::make_shared<ConfigureCommand>());
    dispatcher.registerCommand(std::make_shared<SetBreakpointFilterCommand>());
    dispatcher.registerCommand(std::make_shared<SetBreakpointTypesCommand>());
    dispatcher.registerCommand(std::make_shared<DisconnectCommand>());
}

} // namespace debugger
