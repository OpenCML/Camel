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
 * Updated: Mar. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

/**
 * @file main.cpp
 * @brief 调试器主入口：区分父进程与子进程（--run-worker），完成环境与命令派发初始化后进入 REPL。
 *
 * 设计要点：
 * - 父进程：负责命令行解析、命令模式注册、REPL 循环；实际脚本执行在子进程（spawn）中完成，
 *   以便隔离内存/断点状态，避免与 libcamel 的 GC/Profiler 单例冲突。
 * - 子进程：通过 argv 中的 --run-worker 识别，直接进入 runWorkerMode，不再走 REPL，
 *   在本机指定端口起 HTTP 服务，接收父进程转发的 /api/run 等请求并执行脚本。
 * - atexit + Job Object（spawn 内）确保主进程退出时子进程被系统回收，避免孤儿进程。
 */

#include "windows_parser_guard.h"

#include "camel/utils/dll_path.h"
#include "camel/utils/log.h"
#include "command/commands.h"
#include "command/dispatcher.h"
#include "compile.h"
#include "repl.h"
#include "spawn.h"
#include "state.h"
#include "worker.h"

#ifndef NDEBUG
#include "camel/core/debug_breakpoint.h"
#endif

#include <cctype>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;

/// 判断字符串是否为正整数（用于解析 --serve 的 port 参数），避免把负数或非数字当作端口。
static bool isPositiveInt(const std::string &s) {
    if (s.empty())
        return false;
    for (char c : s)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    return true;
}

int main(int argc, char *argv[]) {
    // 子进程入口：由父进程 spawn 时传入 --run-worker <script>，此处直接进入 worker 模式并退出，
    // 不启动 REPL、不解析其它全局选项，保证子进程行为单一（只跑脚本 + HTTP 服务）。
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--run-worker")
            return debugger::runWorkerMode(argc, argv);
    }

    camel::utils::setupLibrarySearchPath();
    // 行缓冲关闭，便于 pipe/日志实时输出到 Web UI 或控制台，避免输出延迟。
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    // 任意退出路径（return/exit）都会执行：关闭 Job、终止子进程，防止主进程退出后子进程残留。
    std::atexit([] { debugger::terminateAllWorkers(); });

    // 解析可执行路径并保存，供 spawn 子进程时构造命令行（子进程需用同一 exe 启动 --run-worker）。
    // 优先使用绝对路径或基于 getExecutableDirectory 的路径，避免工作目录变化导致找不到 exe。
    {
        fs::path argv0(argv[0]);
        if (argv0.is_absolute() && fs::exists(argv0)) {
            debugger::setExePath(argv0.string());
        } else {
            fs::path dir = camel::utils::getExecutableDirectory();
            if (!dir.empty())
                debugger::setExePath((dir / argv0.filename()).string());
            std::string &exe = debugger::getExePath();
            if (exe.empty() || !fs::exists(exe)) {
                fs::path abs = fs::absolute(argv0);
                if (fs::exists(abs))
                    exe = abs.string();
            }
        }
    }

    EXEC_WHEN_DEBUG({
        // Debug 构建下将 libcamel 的 assert 转为抛异常，便于在调试器中捕获并上报给 Web UI
        cml::set_assert_handler([](const std::string &expression,
                                   const std::string &suggestion,
                                   const std::source_location &location) {
            throw cml::AssertionFailure(expression, suggestion, location);
        });
    });

    // 命令模式：REPL 与 HTTP 的写操作统一经 dispatcher 派发，保证状态一致并可回显。
    auto &dispatcher = debugger::getDispatcher();
    debugger::registerAllCommands(dispatcher);
    // 命令执行后回显到控制台并写入 Logger，使 Web UI 的 Debugger 日志 tab 也能看到 REPL
    // 侧执行的命令。
    dispatcher.setPostExecuteHook(
        [](const std::string &, const std::string &, const debugger::CommandResult &result) {
            if (!result.command.empty()) {
                std::string line = "(camel-db) " + result.command;
                std::cout << line << std::endl;
                Logger::WriteToAllStreams(line);
            }
            if (!result.message.empty()) {
                std::cout << result.message << std::endl;
                Logger::WriteToAllStreams(result.message);
            }
        });

    // 查询类 API（getState、getGirJson）不经过命令派发，由 HTTP 层直接回调，减少间接层且与 DAP
    // 风格一致。
    debugger::DebuggerServer &srv = debugger::getServer();
    srv.setQueryCallbacks(debugger::getStateJson, debugger::getGirJson);

    EXEC_WHEN_DEBUG({
        // 父进程仅注册 breakpoint type（供 GET /api/breakpoint-types 返回 known types）
        camel::DebugBreakpoint::RegisterType("alloc");
        camel::DebugBreakpoint::RegisterType("alloc_before");
    });

    // Ctrl+C 仅设标志位，不直接 exit，以便 REPL 在 getline 失败时检测到后主动 stop
    // 服务器并退出，做一次干净收尾。
    std::signal(SIGINT, [](int) { debugger::setInterrupted(true); });

    auto &opts = debugger::getStartupOptions();

    // 解析命令行填入 StartupOptions，REPL 启动时通过 printStartupBanner 派发
    // startServer/loadSource/configure/launch，统一走命令模式。
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            debugger::printHelp();
            return 0;
        }
        if (arg == "-v" || arg == "--version") {
            debugger::printVersion();
            return 0;
        }
        if (arg == "-V" || arg == "--verbose") {
            opts.verbose = true;
        } else if (arg == "-s" || arg == "--serve") {
            opts.serve = true;
            if (i + 1 < argc && isPositiveInt(argv[i + 1])) {
                opts.port = std::atoi(argv[i + 1]);
                ++i;
            }
        } else if (arg == "-f" || arg == "--file") {
            if (i + 1 < argc)
                opts.file = argv[++i];
        } else if (arg == "-r" || arg == "--run") {
            opts.run = true;
        } else if (arg == "--logfile") {
            if (i + 1 < argc)
                opts.logFile = argv[++i];
        } else if (!arg.empty() && arg[0] != '-') {
            if (opts.file.empty())
                opts.file = arg;
        }
    }

    EXEC_WHEN_DEBUG({
        if (!opts.serve)
            Logger::AddOutputStream(&std::cout);
    });

    debugger::repl();

    // 显式收尾：atexit 也会调一次，这里先清子进程再停 HTTP，避免停服后仍有请求依赖子进程。
    debugger::terminateAllWorkers();
    if (srv.isRunning())
        srv.stop();
    return 0;
}
