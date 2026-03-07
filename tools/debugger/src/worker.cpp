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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * @file worker.cpp
 * @brief 子进程（--run-worker）入口：在本机端口起 HTTP 服务，通过 WorkerRunHandler 接收父进程下发的
 * run 参数并执行脚本。
 *
 * 设计要点：
 * - 父进程 spawn 后 POST /api/run 触发执行；WorkerRunHandler 置位 PendingRun.hasRun，主循环
 *   执行 runScriptOnce。内存扫描与断点由 runScriptOnce
 * 内按统一断点模型处理，各类型断点配置由父进程在 run 后同步。
 * - 子进程同样注册 Command 与 postExecuteHook，使 settings/breakpoint-types
 * 等请求在子进程内执行并回显到子进程 LogSink，供 Task 日志 tab 展示。
 */

#include "windows_parser_guard.h"

#include "nlohmann/json.hpp"

#include "camel/compile/gir.h"
#include "camel/utils/dll_path.h"
#include "camel/utils/log.h"
#include "command/commands.h"
#include "command/dispatcher.h"
#include "compile.h"
#include "gir_json.h"
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
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <format>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <stdlib.h> // _dupenv_s
#endif

namespace mm = camel::core::mm;

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

/// 父进程 POST /api/run 触发一次执行，由 WorkerRunHandler 置位、主循环消费。
/// body 可含 passes（字符串数组），与 CLI 的 Run::targetFiles[1:] 一致；空则 applyPasses 用
/// std::default。
struct PendingRun {
    bool hasRun = false;
};
static std::mutex g_pendingMutex;
static std::condition_variable g_pendingCond;
static PendingRun g_pendingRun;

static void parseRunBodyAndSetPasses(const std::string &body) {
    try {
        auto j  = nlohmann::json::parse(body.empty() ? "{}" : body);
        auto it = j.find("passes");
        if (it != j.end() && it->is_array()) {
            std::vector<std::string> passes;
            for (const auto &e : *it)
                if (e.is_string())
                    passes.push_back(e.get<std::string>());
            getState().runPasses = std::move(passes);
        }
    } catch (...) {
        getState().runPasses.clear();
    }
}
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

    EXEC_WHEN_DEBUG({
        cml::set_assert_handler([](const std::string &expression,
                                   const std::string &suggestion,
                                   const std::source_location &location) {
            throw cml::AssertionFailure(expression, suggestion, location);
        });
    });
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
    getServer().setQueryCallbacks(
        getStateJson,
        [](const std::string &path, const std::string &graphId) {
            return getGirJson(path, graphId);
        });
    EXEC_WHEN_DEBUG({
        camel::DebugBreakpoint::RegisterType("alloc");
        camel::DebugBreakpoint::RegisterType("alloc_before");
        camel::DebugBreakpoint::RegisterType("gir_node");
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
                return;
            }
            if (std::strcmp(type, "gir_node") == 0 && ctx != nullptr) {
                auto *node           = static_cast<const GIR::Node *>(ctx);
                uintptr_t ptr        = reinterpret_cast<uintptr_t>(node);
                std::string stableId = getStableNodeId(node);
                if (getServer().isGirBreakpointNode(ptr) ||
                    getServer().isGirBreakpointNodeStable(stableId)) {
                    std::string nodeId = std::format("0x{:x}", ptr);
                    std::string graphId =
                        std::format("0x{:x}", reinterpret_cast<uintptr_t>(&node->graph()));
                    getServer().pauseAndWaitForGirBreakpoint(nodeId, graphId);
                }
                return;
            }
            getServer().pauseAndWaitForPipelineStage(type);
        });
    });

    getServer().setWorkerRunHandler([](const std::string &body, std::string &responseBody) {
        parseRunBodyAndSetPasses(body);
        {
            std::lock_guard<std::mutex> lock(g_pendingMutex);
            g_pendingRun.hasRun = true;
        }
        g_pendingCond.notify_one();
        nlohmann::json out;
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
        RunOutcome outcome = runScriptOnce(path);
        (void)outcome;
    }

    getServer().stop();
    return 0;
}

} // namespace debugger
