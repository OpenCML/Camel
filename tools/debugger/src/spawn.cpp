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
#include "spawn.h"
#include "state.h"

#include <filesystem>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace debugger {

#if defined(_WIN32)
static std::unordered_map<int, HANDLE> g_workerProcesses; // port -> HANDLE, 支持多 worker 并发
static std::mutex g_workerMutex;
static HANDLE g_job = nullptr;

namespace {
/// 创建 Job 并设置 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE：关闭 Job 时所有关联进程被终止，
/// 这样主进程异常退出（未调 terminateAllWorkers）时系统仍会回收子进程。
void ensureJobExists() {
    if (g_job != nullptr)
        return;
    g_job = CreateJobObjectA(nullptr, nullptr);
    if (g_job == nullptr)
        return;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info = {};
    info.BasicLimitInformation.LimitFlags     = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    SetInformationJobObject(g_job, JobObjectExtendedLimitInformation, &info, sizeof(info));
}
} // namespace

namespace {
/// 从子进程 stdout/stderr 管道读取内容，按行追加到该任务日志缓冲（appendTaskLogLine），不写入父进程
/// Logger， 以便 Debugger tab 仅显示父进程输出、Task tab 仅显示该子进程输出。
void pipeReaderThread(int workerPort, HANDLE hRead) {
    std::string line;
    char buf[512];
    DWORD n = 0;
    while (ReadFile(hRead, buf, sizeof(buf) - 1, &n, nullptr) && n > 0) {
        buf[n] = '\0';
        for (char *p = buf; *p; ++p) {
            if (*p == '\r')
                continue;
            if (*p == '\n') {
                if (!line.empty()) {
                    appendTaskLogLine(workerPort, line);
                    line.clear();
                }
            } else {
                line += *p;
            }
        }
    }
    if (!line.empty())
        appendTaskLogLine(workerPort, line);
    CloseHandle(hRead);
}
} // namespace
#endif

std::pair<bool, int> spawnWorker(
    const std::string &path, bool memoryMonitor, bool allocStep,
    const std::unordered_set<std::string> &breakSpaces) {
    (void)breakSpaces;
    auto &st = getState();
    if (!st.hasFile() && path.empty())
        return {false, 0};
    std::string scriptPath = path.empty() ? st.targetFile : path;
    if (scriptPath.empty() || !fs::exists(scriptPath))
        return {false, 0};
    if (getExePath().empty())
        return {false, 0};

    // 动态分配端口：从 parent_port+1 开始，跳过已被现有 worker 占用的端口。
    int basePort   = getServer().port();
    int workerPort = basePort + 1;
    {
        auto tasks = getTasks();
        std::unordered_set<int> usedPorts;
        for (const auto &t : tasks)
            usedPorts.insert(t.port);
        while (usedPorts.count(workerPort))
            ++workerPort;
    }
#if defined(_WIN32)

    // 创建管道：子进程 stdout/stderr 重定向到 hChildOutW，父进程从 hChildOutR 读，pipeReaderThread
    // 追加到该任务日志缓冲供 Task tab 显示。
    HANDLE hChildOutR = nullptr, hChildOutW = nullptr;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    if (!CreatePipe(&hChildOutR, &hChildOutW, &sa, 0))
        return {false, 0};
    SetHandleInformation(hChildOutR, HANDLE_FLAG_INHERIT, 0); // 读端不继承，避免子进程持有

    std::string cmdLine;
    cmdLine.reserve(getExePath().size() + scriptPath.size() + 32);
    if (getExePath().find(' ') != std::string::npos)
        cmdLine += '"';
    cmdLine += getExePath();
    if (getExePath().find(' ') != std::string::npos)
        cmdLine += '"';
    cmdLine += " --run-worker ";
    if (scriptPath.find(' ') != std::string::npos)
        cmdLine += '"';
    cmdLine += scriptPath;
    if (scriptPath.find(' ') != std::string::npos)
        cmdLine += '"';

    std::string envBlock;
    {
        char *cur = GetEnvironmentStringsA();
        if (cur) {
            const char *p = cur;
            while (p[0] != '\0' || p[1] != '\0')
                envBlock += *p++;
            envBlock += '\0';
            FreeEnvironmentStringsA(cur);
        }
    }
    envBlock += "CAMEL_DB_WORKER_PORT=" + std::to_string(workerPort) + "\0";
    envBlock += '\0';

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');
    STARTUPINFOA si        = {};
    si.cb                  = sizeof(STARTUPINFOA);
    si.dwFlags             = STARTF_USESTDHANDLES;
    si.hStdInput           = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput          = hChildOutW; // 子进程 stdout/stderr 都进管道，便于父进程统一收集
    si.hStdError           = hChildOutW;
    PROCESS_INFORMATION pi = {};
    ensureJobExists(); // 保证子进程加入 Job，主进程退出时能被系统回收
    BOOL ok = CreateProcessA(
        nullptr,
        cmdBuf.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        (LPVOID)envBlock.c_str(),
        nullptr,
        &si,
        &pi);
    CloseHandle(hChildOutW);
    hChildOutW = nullptr;
    if (!ok) {
        CloseHandle(hChildOutR);
        return {false, 0};
    }
    CloseHandle(pi.hThread);
    if (g_job != nullptr)
        AssignProcessToJobObject(g_job, pi.hProcess);
    {
        std::lock_guard<std::mutex> lock(g_workerMutex);
        g_workerProcesses[workerPort] = pi.hProcess;
    }
    registerTask(workerPort, scriptPath);
    std::thread(pipeReaderThread, workerPort, hChildOutR).detach();
    std::thread([workerPort]() {
        HANDLE hProc = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_workerMutex);
            auto it = g_workerProcesses.find(workerPort);
            if (it != g_workerProcesses.end())
                hProc = it->second;
        }
        if (hProc) {
            WaitForSingleObject(hProc, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(hProc, &code);
            CloseHandle(hProc);
            {
                std::lock_guard<std::mutex> lock(g_workerMutex);
                g_workerProcesses.erase(workerPort);
            }
            setTaskState(workerPort, "exited");
        }
    }).detach();
    getTaskState() = "running";
    return {true, workerPort};
#else
    (void)memoryMonitor;
    (void)allocStep;
    (void)workerPort;
    return {false, 0};
#endif
}

void terminateAllWorkers() {
#if defined(_WIN32)
    {
        std::lock_guard<std::mutex> lock(g_workerMutex);
        for (auto &[port, hProc] : g_workerProcesses) {
            if (hProc)
                TerminateProcess(hProc, 0);
        }
    }
    if (g_job) {
        CloseHandle(g_job);
        g_job = nullptr;
    }
#endif
}

bool terminateWorker(int port) {
#if defined(_WIN32)
    HANDLE hProc = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_workerMutex);
        auto it = g_workerProcesses.find(port);
        if (it != g_workerProcesses.end())
            hProc = it->second;
    }
    if (hProc) {
        TerminateProcess(hProc, 0);
        return true;
    }
    return false;
#else
    (void)port;
    return false;
#endif
}

} // namespace debugger
