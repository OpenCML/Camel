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
 * Updated: Mar. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "windows_parser_guard.h"

// nlohmann MUST be included before any header that pulls in ANTLR (EOF conflict)
#include "nlohmann/json.hpp"

#include "command.h"
#include "commands.h"
#include "dispatcher.h"

#include "camel/core/debug_breakpoint.h"
#include "camel/utils/log.h"
#include "spawn.h"
#include "state.h"

#ifndef NDEBUG
#include "camel/core/mm/debug_hook.h"
#endif

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <memory>
#include <sstream>
#include <thread>

namespace fs = std::filesystem;

namespace debugger {

using json = nlohmann::json;

/// 将父进程保存的断点状态（节点型）推送到指定端口的 worker，供 Run/Restart
/// 后继承；统一断点模型下各类型断点均需在 spawn/run 后同步。
static void pushGirBreakpointsToPort(DebuggerServer &srv, int port) {
    auto girIds = srv.getGirBreakpointNodeIds();
    if (girIds.empty())
        return;
    json girBody;
    girBody["nodeIds"] = json::array();
    for (uintptr_t id : girIds) {
        std::ostringstream o;
        o << "0x" << std::hex << id;
        girBody["nodeIds"].push_back(o.str());
    }
    srv.forwardPostToPort(port, "/api/gir-breakpoints", girBody.dump());
}

/// 将父进程保存的断点类型列表（含 pipeline 阶段等）推送到指定端口的 worker。
static void pushBreakpointTypesToPort(DebuggerServer &srv, int port) {
    auto enabled = srv.getEnabledBreakpointTypes();
    if (enabled.empty())
        return;
    json body;
    body["enabled"] = enabled;
    srv.forwardPostToPort(port, "/api/breakpoint-types", body.dump());
}

/// 将父进程保存的断点状态（alloc spaces、GIR 节点、断点类型）一次性同步到指定端口的 worker。
/// 用于新 spawn 或 restart 后的子进程，在发送 /api/run 之前调用，确保新进程具备完整断点配置。
static void syncBreakpointStateToPort(DebuggerServer &srv, int port) {
    json pushBody;
    pushBody["breakSpaces"] = json::array();
    for (const auto &s : srv.getAllocBreakSpaces())
        pushBody["breakSpaces"].push_back(s);
    srv.forwardPostToPort(port, "/api/breakpoint-spaces", pushBody.dump());
    pushGirBreakpointsToPort(srv, port);
    pushBreakpointTypesToPort(srv, port);
}

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

        // 打开文件即 spawn 子进程并注册为 loaded 任务；同一文件可多次打开，每次新建 worker。
        if (!getServer().isWorkerProcess()) {
            auto [ok, workerPort] = spawnWorker(path, false);
            if (ok && workerPort > 0) {
                setForegroundTaskId(std::to_string(workerPort));
                json body;
                body["ok"]     = true;
                body["taskId"] = std::to_string(workerPort);
                return CommandResult::ok(
                    display,
                    "Loaded: " + path + " (task " + std::to_string(workerPort) + ").",
                    body.dump());
            }
        }
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
        if (getServer().isWorkerProcess())
            return CommandResult::error("run", "Error: launch is not available in worker process.");

        json args           = json::parse(argsJson, nullptr, false);
        std::string target  = args.value("target", "");
        std::string display = "run";

        int port = resolveTargetToPort(target);
        if (port > 0) {
            auto tasks = getTasks();
            auto it    = std::find_if(tasks.begin(), tasks.end(), [port](const TaskInfo &t) {
                return t.port == port;
            });
            if (it != tasks.end() && it->taskState == "loaded") {
                std::string runBody = argsJson.empty() ? "{}" : argsJson;
                if (!getServer().isWorkerProcess())
                    getServer().setLastRunBody(runBody);
                // 已加载任务：用户在前端设置的断点已实时同步到 worker，run 时无需再同步。
                for (int retry = 0; retry < 15; ++retry) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    if (getServer().forwardPostToPort(port, "/api/run", runBody)) {
                        setTaskState(port, "running");
                        return CommandResult::ok(
                            display,
                            "Run started (task " + std::to_string(port) + ").");
                    }
                }
                return CommandResult::error(
                    display,
                    "Run forward to task " + std::to_string(port) + " failed.");
            }
            if (it != tasks.end() && it->taskState != "exited")
                return CommandResult::error(
                    display,
                    "Task " + std::to_string(port) + " is not in loaded state.");
        }

        // 无 target 或任务不存在/已退出：fallback 为 spawn 新 worker（需已有 targetFile）
        std::string path = getState().targetFile;
        if (path.empty() || !getState().hasFile())
            return CommandResult::error(display, "Error: no task selected or load a file first.");

        auto [ok, workerPort] = spawnWorker(path, true);
        if (ok && workerPort > 0) {
            getServer().setChildPort(workerPort);
            std::string runBody = argsJson.empty() ? "{}" : argsJson;
            // 新 spawn 的 worker 无状态，先同步断点再 run。
            syncBreakpointStateToPort(getServer(), workerPort);
            for (int retry = 0; retry < 15; ++retry) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                if (getServer().forwardPostToPort(workerPort, "/api/run", runBody)) {
                    return CommandResult::ok(
                        display,
                        "Run started (worker port " + std::to_string(workerPort) + ").");
                }
            }
        }
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

    CommandResult execute(const std::string &argsJson) override {
        auto &srv = getServer();
        if (srv.isWorkerProcess()) {
            srv.requestContinue();
            return CommandResult::ok("continue", "Continue.");
        }
        json args          = json::parse(argsJson, nullptr, false);
        std::string target = args.value("target", "");
        int port           = resolveTargetToPort(target);
        if (port <= 0)
            port = srv.getChildPort();
        if (port <= 0)
            return CommandResult::error(
                "continue",
                "No task selected or target invalid. Select a task or pass target.");
        if (!srv.forwardPostToPort(port, "/api/continue", argsJson.empty() ? "{}" : argsJson))
            return CommandResult::error(
                "continue",
                "Target unreachable (connection failed or timeout).");
        return CommandResult::ok("continue", "Continue (task " + std::to_string(port) + ").");
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
        // 父进程：按 target 终止对应 worker（若未退出）并重新 spawn；支持已 exited 任务（直接
        // respawn）
        json args          = json::parse(argsJson, nullptr, false);
        std::string target = args.value("target", "");
        int port           = resolveTargetToPort(target);
        if (port <= 0)
            port = getTaskPortByTargetIncludingExited(target);
        if (port <= 0)
            return CommandResult::error(
                "restart",
                "No task selected or target invalid. Select a task in the sidebar or pass target.");
        std::vector<TaskInfo> tasks = getTasks();
        std::string scriptPath;
        std::string taskState;
        for (const auto &t : tasks)
            if (t.port == port) {
                scriptPath = t.scriptPath;
                taskState  = t.taskState;
                break;
            }
        if (scriptPath.empty())
            return CommandResult::error("restart", "Task not found.");
        if (taskState != "exited") {
            if (!terminateWorker(port))
                return CommandResult::error("restart", "Failed to terminate worker.");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        auto [ok, newPort] = spawnWorker(scriptPath, true, port);
        if (!ok || newPort <= 0)
            return CommandResult::error("restart", "Respawn failed.");
        setTaskState(port, "running");
        std::string runBody = srv.getLastRunBody();
        if (runBody.empty())
            runBody = "{}";
        // Restart 使用新子进程，无状态，先同步断点再 run。
        syncBreakpointStateToPort(srv, port);
        bool runSent = false;
        for (int retry = 0; retry < 15; ++retry) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (srv.forwardPostToPort(port, "/api/run", runBody)) {
                runSent = true;
                break;
            }
        }
        (void)runSent;
        return CommandResult::ok("restart", "Restart (task " + std::to_string(port) + ").");
    }
};

// ---------------------------------------------------------------------------
// terminate — terminate execution
// ---------------------------------------------------------------------------
class TerminateCommand final : public Command {
  public:
    const char *name() const override { return "terminate"; }
    const char *description() const override { return "Terminate execution"; }

    CommandResult execute(const std::string &argsJson) override {
        auto &srv = getServer();
        if (srv.isWorkerProcess()) {
            srv.requestTerminate();
            return CommandResult::ok("terminate", "Terminate requested.");
        }
        json args          = json::parse(argsJson, nullptr, false);
        std::string target = args.value("target", "");
        int port           = resolveTargetToPort(target);
        if (port <= 0)
            port = srv.getChildPort();
        if (port <= 0)
            return CommandResult::error(
                "terminate",
                "No task selected or target invalid. Select a task or pass target.");
        if (!srv.forwardPostToPort(port, "/api/terminate", argsJson.empty() ? "{}" : argsJson))
            return CommandResult::error(
                "terminate",
                "Target unreachable (connection failed or timeout).");
        return CommandResult::ok("terminate", "Terminate (task " + std::to_string(port) + ").");
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
        auto &srv = getServer();
        if (!srv.isWorkerProcess()) {
            std::string target = args.value("target", "");
            int port           = resolveTargetToPort(target);
            if (port > 0 &&
                srv.forwardPostToPort(port, "/api/settings", argsJson.empty() ? "{}" : argsJson))
                msg += " (task " + std::to_string(port) + ")";
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
    const char *description() const override { return "Set breakpoint filters (alloc space type)"; }

    CommandResult execute(const std::string &argsJson) override {
        json args = json::parse(argsJson, nullptr, false);
        std::unordered_set<std::string> breakSpaces;
        if (args.contains("breakSpaces") && args["breakSpaces"].is_array())
            for (const auto &v : args["breakSpaces"])
                if (v.is_string() && !v.get<std::string>().empty())
                    breakSpaces.insert(v.get<std::string>());

        std::string display = "alloc-breakpoint-spaces";
        std::string msg;
        if (breakSpaces.empty()) {
            msg = "Alloc breakpoint spaces: (none)";
        } else {
            msg        = "Alloc breakpoint spaces: ";
            bool first = true;
            for (const auto &b : breakSpaces) {
                if (!first)
                    msg += ", ";
                msg += b;
                first = false;
            }
        }

        auto &srv = getServer();
        srv.setAllocBreakSpaces(std::move(breakSpaces));
        if (!srv.isWorkerProcess()) {
            std::string target = args.value("target", "");
            int port           = resolveTargetToPort(target);
            if (port > 0 && srv.forwardPostToPort(
                                port,
                                "/api/breakpoint-spaces",
                                argsJson.empty() ? "{}" : argsJson))
                msg += " (task " + std::to_string(port) + ")";
        }
        return CommandResult::ok(display, msg);
    }
};

// ---------------------------------------------------------------------------
// breakpointStatus — 按类型查看当前断点状态（alloc / pipeline 等）
// ---------------------------------------------------------------------------
class BreakpointStatusCommand final : public Command {
  public:
    const char *name() const override { return "breakpointStatus"; }
    const char *description() const override {
        return "Show breakpoint status by type (alloc, etc.)";
    }

    CommandResult execute(const std::string &) override {
        auto spaces     = getServer().getAllocBreakSpaces();
        std::string msg = "alloc: ";
        if (spaces.empty()) {
            msg += "(none)";
        } else {
            bool first = true;
            for (const auto &s : spaces) {
                if (!first)
                    msg += ", ";
                msg += s;
                first = false;
            }
        }
        return CommandResult::ok("breakpoints", msg);
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

        // 断点按统一模型由 runScriptOnce 与 Run/Restart 时的同步决定

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
        auto &srv = getServer();
        if (!srv.isWorkerProcess()) {
            srv.setEnabledBreakpointTypes(enabled);
            std::string target = args.value("target", "");
            int port           = resolveTargetToPort(target);
            if (port > 0 && srv.forwardPostToPort(
                                port,
                                "/api/breakpoint-types",
                                argsJson.empty() ? "{}" : argsJson))
                msg += " (task " + std::to_string(port) + ")";
        }
        return CommandResult::ok("breakpoint-types", msg);
    }
};

// ---------------------------------------------------------------------------
// task — list all tasks or print detailed state of foreground/specified task
// ---------------------------------------------------------------------------
class TaskCommand final : public Command {
  public:
    const char *name() const override { return "task"; }
    const char *description() const override { return "List tasks or show task state"; }

    CommandResult execute(const std::string &argsJson) override {
        if (getServer().isWorkerProcess())
            return CommandResult::error(
                "task",
                "task command is only available in the main process.");

        json args          = json::parse(argsJson, nullptr, false);
        std::string sub    = args.value("subcommand", "");
        std::string target = args.value("target", "");

        if (sub == "list") {
            auto tasks = getTasks();
            if (tasks.empty()) {
                return CommandResult::ok("task", "No tasks.");
            }
            std::ostringstream oss;
            oss << "Tasks (" << tasks.size() << "):\n";
            for (const auto &t : tasks) {
                oss << "  id=" << t.id << " port=" << t.port << " state=" << t.taskState
                    << " path=" << t.scriptPath << "\n";
            }
            return CommandResult::ok("task", oss.str());
        }

        if (sub == "state") {
            std::string effective = target.empty() ? getForegroundTaskId() : target;
            if (effective.empty()) {
                return CommandResult::error(
                    "task",
                    "No target specified and no foreground task set. Use 'task <id>' to set "
                    "foreground or 'task state <id>'.");
            }
            auto tasks            = getTasks();
            const TaskInfo *found = nullptr;
            for (const auto &t : tasks) {
                if (t.id == effective || std::to_string(t.port) == effective) {
                    found = &t;
                    break;
                }
            }
            if (!found) {
                return CommandResult::error("task", "Task not found: " + effective);
            }
            std::ostringstream oss;
            oss << "Task " << found->id << ":\n"
                << "  port: " << found->port << "\n"
                << "  scriptPath: " << found->scriptPath << "\n"
                << "  taskState: " << found->taskState << "\n";
            return CommandResult::ok("task", oss.str());
        }

        return CommandResult::error("task", "Usage: task list | task state [target]");
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
            EXEC_WHEN_DEBUG({ mm::clearPostAllocDebugHook(); });
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
    dispatcher.registerCommand(std::make_shared<BreakpointStatusCommand>());
    dispatcher.registerCommand(std::make_shared<SetBreakpointTypesCommand>());
    dispatcher.registerCommand(std::make_shared<TaskCommand>());
    dispatcher.registerCommand(std::make_shared<DisconnectCommand>());
}

} // namespace debugger
