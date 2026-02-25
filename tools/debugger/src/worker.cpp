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
 * Updated: Feb. 26, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * @file worker.cpp
 * @brief 子进程（--run-worker）入口：在本机端口起 HTTP 服务，通过 WorkerRunHandler 接收父进程下发的
 * run 参数并执行脚本。
 *
 * 设计要点：
 * - run 参数（memoryMonitor/allocStep/breakSpaces）不再经环境变量传递，而是父进程 spawn 后 POST
 * /api/run 到子进程， 子进程在 WorkerRunHandler 中解析 body 并写入 PendingRun，主循环 wait 到
 * hasRun 后执行 runScriptOnce，便于 Web UI 动态改选项。
 * - 子进程同样注册 Command 与 postExecuteHook，使 settings/breakpoint-types
 * 等请求在子进程内执行并回显到子进程 LogSink，供 Task 日志 tab 展示。
 */

#include "nlohmann/json.hpp"

#include "camel/utils/dll_path.h"
#include "camel/utils/log.h"
#include "command/commands.h"
#include "command/dispatcher.h"
#include "compile.h"
#include "run.h"
#include "state.h"
#include "worker.h"

#ifndef NDEBUG
#include "camel/core/debug_breakpoint.h"
#include "camel/core/mm/debug_hook.h"
#include "camel/utils/assert.h"
#endif

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

#if defined(_WIN32)
#include <stdlib.h> // _dupenv_s
#endif

namespace debugger {

namespace {
#if defined(_WIN32)
std::string getEnvValue(const char *name) {
    char *val  = nullptr;
    size_t len = 0;
    if (_dupenv_s(&val, &len, name) == 0 && val != nullptr) {
        std::string s(val);
        free(val);
        return s;
    }
    if (val)
        free(val);
    return std::string();
}
#else
std::string getEnvValue(const char *name) {
    const char *p = std::getenv(name);
    return p ? std::string(p) : std::string();
}
#endif

/// 父进程 POST /api/run 下发的单次 run 参数，由 WorkerRunHandler
/// 写入、主循环消费，避免子进程启动时尚未收到 body 就执行默认选项。
struct PendingRun {
    bool hasRun        = false;
    bool memoryMonitor = true;
    bool allocStep     = false;
    std::unordered_set<std::string> breakSpaces;
};
static std::mutex g_pendingMutex;
static std::condition_variable g_pendingCond;
static PendingRun g_pendingRun;
} // namespace

int runWorkerMode(int argc, char *argv[]) {
    std::string path;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--run-worker" && i + 1 < argc) {
            path = argv[i + 1];
            break;
        }
    }
    if (path.empty() || !std::filesystem::exists(path)) {
        std::cerr << "Usage: camel-db --run-worker <path.cml>" << std::endl;
        return 1;
    }
    camel::utils::setupLibrarySearchPath();
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // 端口由父进程在 spawn 时通过环境变量传入，子进程与父进程通过该端口通信。
    std::string portStr = getEnvValue("CAMEL_DB_WORKER_PORT");
    int workerPort      = (!portStr.empty()) ? std::atoi(portStr.c_str()) : 0;
    if (workerPort <= 0) {
        std::cerr << "CAMEL_DB_WORKER_PORT not set or invalid." << std::endl;
        return 1;
    }

#ifndef NDEBUG
    cml::set_assert_handler([](const std::string &expression,
                               const std::string &suggestion,
                               const std::source_location &location) {
        throw cml::AssertionFailure(expression, suggestion, location);
    });
#endif
    getState().targetFile = path;
    clearRunState();

    registerAllCommands(getDispatcher());
    // 子进程内执行的命令（如 Web UI 下发的 configure/setBreakpointTypes）回显到本进程 Logger，供
    // GET /api/log?target= 拉取并在 Task 日志 tab 显示。
    getDispatcher().setPostExecuteHook(
        [](const std::string &, const std::string &, const CommandResult &result) {
            if (!result.command.empty()) {
                std::string line = "(camel-db) " + result.command;
                Logger::WriteToAllStreams(line + "\n");
            }
            if (!result.message.empty())
                Logger::WriteToAllStreams(result.message + "\n");
        });
    getServer().setQueryCallbacks(getStateJson, getGirDot);
#ifndef NDEBUG
    camel::DebugBreakpoint::RegisterType("alloc");
    camel::DebugBreakpoint::RegisterType("alloc_before");
    camel::DebugBreakpoint::SetHandler([](const char *type, const void *ctx) {
        if (!type)
            return;
        if (std::strcmp(type, "alloc_before") == 0) {
            auto *evt = static_cast<const mm::PreAllocEvent *>(ctx);
            if (evt)
                getServer().pauseAndWaitForContinue(nullptr, evt->size, evt->space);
            return;
        }
        if (std::strcmp(type, "alloc") == 0) {
            auto *evt = static_cast<const mm::AllocEvent *>(ctx);
            if (evt)
                getServer().pauseAndWaitForContinue(evt->ptr, evt->size, evt->space);
        }
    });
#endif

    // 父进程转发 POST /api/run 时由此处处理：解析 body 写入 PendingRun
    // 并唤醒主循环，避免子进程在未收到参数时用默认值执行。
    getServer().setWorkerRunHandler([](const std::string &body, std::string &responseBody) {
        using json         = nlohmann::json;
        json j             = json::parse(body, nullptr, false);
        bool memoryMonitor = j.value("memoryMonitor", true);
        bool allocStep     = j.value("allocStep", false);
        std::unordered_set<std::string> breakSpaces;
        if (j.contains("breakSpaces") && j["breakSpaces"].is_array())
            for (const auto &v : j["breakSpaces"])
                if (v.is_string() && !v.get<std::string>().empty())
                    breakSpaces.insert(v.get<std::string>());

        getServer().setAllocBreakSpaces(breakSpaces);

        {
            std::lock_guard<std::mutex> lock(g_pendingMutex);
            g_pendingRun.hasRun        = true;
            g_pendingRun.memoryMonitor = memoryMonitor;
            g_pendingRun.allocStep     = allocStep;
            g_pendingRun.breakSpaces   = std::move(breakSpaces);
        }
        g_pendingCond.notify_one();

        json out;
        out["ok"]    = true;
        responseBody = out.dump();
    });

    getServer().start(workerPort, true);

    // 主循环：每次收到 POST /api/run 后执行一次 runScriptOnce；200ms 超时以便定期检查
    // isRunning，便于 stop() 后尽快退出。
    while (getServer().isRunning()) {
        PendingRun pending;
        {
            std::unique_lock<std::mutex> lock(g_pendingMutex);
            g_pendingCond.wait_for(lock, std::chrono::milliseconds(200), [] {
                return g_pendingRun.hasRun;
            });
            if (!getServer().isRunning())
                break;
            if (!g_pendingRun.hasRun)
                continue;
            pending             = g_pendingRun;
            g_pendingRun.hasRun = false;
        }
        RunOutcome outcome = runScriptOnce(path, pending.memoryMonitor, pending.allocStep);
        (void)outcome;
    }

    getServer().stop();
    return 0;
}

} // namespace debugger
