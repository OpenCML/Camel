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
#pragma once

/**
 * @file state.h
 * @brief 调试器全局状态与任务表：单例状态（文件、verbose、日志路径）、任务注册与 target 解析。
 *
 * 设计要点：
 * - verbose/allocStepEnabled 等为“当前进程/任务”状态：父进程有任务时 getStateJson
 * 不再输出这些顶层字段，由前端按 target 调 GET /api/settings 获取。
 * - tasks 仅父进程维护；子进程 g_tasks 为空，getStateJson 输出单任务字段以兼容“隐式任务”。
 */

#include "camel/core/context/context.h"
#include "camel/core/module/userdef.h"
#include "camel/parse/parse.h"
#include "server.h"

#include <memory>
#include <string>
#include <vector>

namespace debugger {

/// 父进程侧记录的一个子进程（任务）：id 一般为 port 字符串，供 API 的 target 解析。
struct TaskInfo {
    std::string id;
    int port = 0;
    std::string scriptPath;
    std::string taskState; /// "running" | "paused" | "completed" | "loaded" 等
};

/// 命令行解析结果，REPL 启动时据此派发 startServer/loadSource/configure/launch，避免重复解析。
struct StartupOptions {
    bool serve = false;
    int port   = 8765;
    std::string file;
    bool run     = false;
    bool verbose = false;
    std::string logFile;
};

/// 当前加载的脚本与编译/解析上下文，供 compile、run
/// 等模块使用；与“任务”无关，父进程也可持有一份用于 file 命令。
struct DebuggerState {
    std::string targetFile;
    context_ptr_t ctx;
    std::shared_ptr<UserDefinedModule> mainModule;
    std::shared_ptr<CamelParser> parser;
    /// Pass 列表：与 CLI 的 Run::targetFiles[1:] 一致，空时 applyPasses 使用 std::fallback。
    std::vector<std::string> runPasses;

    bool hasFile() const { return !targetFile.empty(); }
};

StartupOptions &getStartupOptions();
DebuggerState &getState();
std::string &getTaskState();
bool getVerbose();
void setVerbose(bool v);
std::string &getExePath();
void setExePath(const std::string &path);
DebuggerServer &getServer();

/// 返回当前进程的状态 JSON；父进程有任务时不包含顶层
/// verbose/allocStepEnabled/taskState，子进程或无任务时包含。
std::string getStateJson();
void setLogFilePath(const std::string &path);
const std::string &getLogFilePath();
void setSettings(bool verbose, const std::string &logFile);

bool isInterrupted();
void setInterrupted(bool v);

void registerTask(
    int port, const std::string &scriptPath, const std::string &initialState = "running");
void unregisterTask(int port);
void setTaskState(int port, const std::string &taskState);
std::vector<TaskInfo> getTasks();
/// 将 API 的 target（id 或 port 字符串）解析为端口号，空时先看前台任务再回退到单任务，用于转发。
/// 仅返回非 exited 任务的端口；exited 任务返回 0。
int resolveTargetToPort(const std::string &target);
/// 同上，但包含已退出的任务，用于返回已退出任务的缓存数据（如 log、父进程 settings）。
int getTaskPortByTargetIncludingExited(const std::string &target);

/// 主进程 CLI 前台任务：未显式指定 target 时默认作用于此任务。
std::string getForegroundTaskId();
void setForegroundTaskId(const std::string &id);

/// 父进程专用：子进程 stdout/stderr 管道内容按行追加到该任务日志缓冲，供 GET /api/log?target=
/// 返回，与父进程自身 Logger 分离。
void appendTaskLogLine(int port, const std::string &line);
/// 父进程专用：按 offset 分页读取某任务的日志行；nextOffset 为下次请求应使用的 offset。
void getTaskLogLines(int port, size_t offset, std::vector<std::string> &lines, size_t &nextOffset);

} // namespace debugger
