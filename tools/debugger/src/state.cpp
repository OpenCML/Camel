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
 * 实现 state.h 声明的全局状态与任务表。getStateJson 根据“是否有任务”决定是否输出
 * 顶层
 * taskState/verbose/allocStepEnabled/allocBreakpointsAvailable，以便前端将这类字段视为任务级并从
 * GET /api/settings?target= 获取。
 */

#include "windows_parser_guard.h"

#include "camel/utils/log.h"
#include "state.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace debugger {

// -----------------------------------------------------------------------------
// 单例状态（父/子进程各有一份，子进程不注册 tasks，故 g_tasks 为空）
// -----------------------------------------------------------------------------
static StartupOptions g_startupOptions;
static DebuggerState g_state;
static DebuggerServer g_server;
static std::string g_taskState =
    "idle"; /// 无任务或单进程时的逻辑状态，有任务时以 tasks[].taskState 为准
static bool g_verbose = false;
static std::string g_exePath;
static std::atomic<bool> g_interrupted{false};
static std::string g_logFilePath;
static size_t g_logStreamHandle = 0;
static std::shared_ptr<std::ofstream> g_logFileStream;

static std::vector<TaskInfo> g_tasks;
static std::mutex g_tasksMutex;

static std::string g_foregroundTaskId;
static std::mutex g_foregroundMutex;

/// 父进程按任务端口缓冲子进程管道输出，与父进程自身 Logger 分离，供 GET /api/log?target= 使用。
static constexpr size_t kTaskLogMaxLines = 2000;
static std::unordered_map<int, std::deque<std::string>> g_taskLogBuffers;
static std::mutex g_taskLogMutex;

StartupOptions &getStartupOptions() { return g_startupOptions; }
DebuggerState &getState() { return g_state; }
std::string &getTaskState() { return g_taskState; }
bool getVerbose() { return g_verbose; }
void setVerbose(bool v) { g_verbose = v; }
std::string &getExePath() { return g_exePath; }
void setExePath(const std::string &path) { g_exePath = path; }
DebuggerServer &getServer() { return g_server; }

void setLogFilePath(const std::string &path) {
    // 先移除已有文件流，再按新 path 注册，避免重复注册或泄漏 handle。
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

const std::string &getLogFilePath() { return g_logFilePath; }

std::string getStateJson() {
    auto &st = getState();
    std::string taskState =
        !st.hasFile() ? "idle" : (getServer().isPaused() ? "paused" : g_taskState);
    std::ostringstream oss;
    oss << "{\"serverRunning\":" << (getServer().isRunning() ? "true" : "false")
        << ",\"hasFile\":" << (st.hasFile() ? "true" : "false");
    // 仅在本进程“没有任务”时输出任务级字段：子进程 g_tasks 为空，父进程无 spawn 时也为空，
    // 此时 state 代表唯一的“隐式任务”，前端可直接用；父进程有任务时这些字段由 tasks[].taskState 与
    // GET /api/settings?target= 提供。
    bool hasTasks = false;
    {
        std::lock_guard<std::mutex> lock(g_tasksMutex);
        hasTasks = !g_tasks.empty();
    }
    if (!hasTasks) {
        oss << ",\"memoryMonitorRunning\":"
            << (getServer().isMemoryScanRunning() ? "true" : "false") << ",\"taskState\":\""
            << taskState << "\""
            << ",\"verbose\":" << (g_verbose ? "true" : "false")
            << ",\"allocStepEnabled\":" << (getServer().allocStepEnabled() ? "true" : "false");
#ifndef NDEBUG
        oss << ",\"allocBreakpointsAvailable\":true";
#else
        oss << ",\"allocBreakpointsAvailable\":false";
#endif
    }
    if (st.hasFile()) {
        oss << ",\"targetFile\":\"";
        for (char c : st.targetFile) {
            if (c == '\\' || c == '"')
                oss << '\\';
            oss << c;
        }
        oss << "\"";
    } else {
        oss << ",\"targetFile\":\"\"";
    }
    oss << ",\"tasks\":[";
    {
        std::lock_guard<std::mutex> lock(g_tasksMutex);
        for (size_t i = 0; i < g_tasks.size(); ++i) {
            const auto &t = g_tasks[i];
            if (i)
                oss << ",";
            oss << "{\"id\":\"" << t.id << "\",\"port\":" << t.port << ",\"scriptPath\":\"";
            for (char c : t.scriptPath) {
                if (c == '\\' || c == '"')
                    oss << '\\';
                oss << c;
            }
            oss << "\",\"taskState\":\"" << t.taskState << "\"}";
        }
    }
    oss << "]}";
    return oss.str();
}

void setSettings(bool verbose, const std::string &logFile) {
    g_verbose = verbose;
    Logger::SetVerbose(verbose);
    setLogFilePath(logFile);
}

bool isInterrupted() { return g_interrupted.load(); }
void setInterrupted(bool v) { g_interrupted.store(v); }

void registerTask(int port, const std::string &scriptPath, const std::string &initialState) {
    std::lock_guard<std::mutex> lock(g_tasksMutex);
    std::string id = std::to_string(port); // id 与 port 一致，便于 API target 解析
    for (const auto &t : g_tasks)
        if (t.port == port)
            return;
    g_tasks.push_back({id, port, scriptPath, initialState.empty() ? "running" : initialState});
}

void unregisterTask(int port) {
    std::lock_guard<std::mutex> lock(g_tasksMutex);
    g_tasks.erase(
        std::remove_if(
            g_tasks.begin(),
            g_tasks.end(),
            [port](const TaskInfo &t) { return t.port == port; }),
        g_tasks.end());
}

void setTaskState(int port, const std::string &taskState) {
    std::string scriptPath;
    {
        std::lock_guard<std::mutex> lock(g_tasksMutex);
        for (auto &t : g_tasks)
            if (t.port == port) {
                scriptPath  = t.scriptPath;
                t.taskState = taskState;
                break;
            }
    }
    if (taskState == "exited" && !scriptPath.empty()) {
        std::string msg = "Task " + std::to_string(port) + " exited (" + scriptPath + ").\n";
        Logger::WriteToAllStreams(msg);
    }
}

std::vector<TaskInfo> getTasks() {
    std::lock_guard<std::mutex> lock(g_tasksMutex);
    return g_tasks;
}

std::string getForegroundTaskId() {
    std::lock_guard<std::mutex> lock(g_foregroundMutex);
    return g_foregroundTaskId;
}

void setForegroundTaskId(const std::string &id) {
    std::lock_guard<std::mutex> lock(g_foregroundMutex);
    g_foregroundTaskId = id;
}

int resolveTargetToPort(const std::string &target) {
    std::string effective = target;
    if (effective.empty()) {
        std::lock_guard<std::mutex> lock(g_foregroundMutex);
        effective = g_foregroundTaskId;
    }
    std::lock_guard<std::mutex> lock(g_tasksMutex);
    if (!effective.empty()) {
        for (const auto &t : g_tasks)
            if (t.id == effective && t.taskState != "exited")
                return t.port;
        int port = std::atoi(effective.c_str());
        for (const auto &t : g_tasks)
            if (t.port == port && t.taskState != "exited")
                return t.port;
        return 0;
    }
    // 未指定 target 且仅有一个活跃任务时，默认转发到该任务。
    std::vector<const TaskInfo *> alive;
    for (const auto &t : g_tasks)
        if (t.taskState != "exited")
            alive.push_back(&t);
    if (alive.size() == 1)
        return alive[0]->port;
    return 0;
}

int getTaskPortByTargetIncludingExited(const std::string &target) {
    if (target.empty())
        return 0;
    std::lock_guard<std::mutex> lock(g_tasksMutex);
    for (const auto &t : g_tasks)
        if (t.id == target)
            return t.port;
    int port = std::atoi(target.c_str());
    for (const auto &t : g_tasks)
        if (t.port == port)
            return t.port;
    return 0;
}

void appendTaskLogLine(int port, const std::string &line) {
    std::lock_guard<std::mutex> lock(g_taskLogMutex);
    auto &q = g_taskLogBuffers[port];
    q.push_back(line);
    while (q.size() > kTaskLogMaxLines)
        q.pop_front();
}

void getTaskLogLines(int port, size_t offset, std::vector<std::string> &lines, size_t &nextOffset) {
    lines.clear();
    std::lock_guard<std::mutex> lock(g_taskLogMutex);
    auto it = g_taskLogBuffers.find(port);
    if (it == g_taskLogBuffers.end()) {
        nextOffset = 0;
        return;
    }
    const auto &q = it->second;
    if (offset >= q.size()) {
        nextOffset = offset;
        return;
    }
    for (size_t i = offset; i < q.size(); ++i)
        lines.push_back(q[i]);
    nextOffset = offset + lines.size();
}

} // namespace debugger
