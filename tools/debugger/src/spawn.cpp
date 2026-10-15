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
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "spawn.h"
#include "child_process.h"
#include "state.h"

#include <httplib.h>

#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace fs = std::filesystem;

namespace debugger {

namespace {

std::unordered_map<int, std::shared_ptr<ChildProcess>> g_workers; // port -> process
std::mutex g_workerMutex;

/// Splits the output of a worker into lines for that task's log buffer (the Task tab), apart
/// from the gateway's own log.
struct LineSplitter {
    int port = 0;
    std::mutex mutex;
    std::string line;

    void feed(std::string_view chunk) {
        std::lock_guard<std::mutex> lock(mutex);
        for (char c : chunk) {
            if (c == '\r')
                continue;
            if (c == '\n') {
                if (!line.empty()) {
                    appendTaskLogLine(port, line);
                    line.clear();
                }
            } else {
                line += c;
            }
        }
    }
    void flush() {
        std::lock_guard<std::mutex> lock(mutex);
        if (!line.empty()) {
            appendTaskLogLine(port, line);
            line.clear();
        }
    }
};

} // namespace

std::pair<bool, int> spawnWorker(const std::string &path, bool sendRun, int desiredPort) {
    auto &st = getState();
    if (!st.hasFile() && path.empty())
        return {false, 0};
    std::string scriptPath = path.empty() ? st.targetFile : path;
    if (scriptPath.empty() || !fs::exists(scriptPath))
        return {false, 0};
    if (getExePath().empty())
        return {false, 0};
    // The worker gets an absolute path: its working directory need not be the gateway's.
    std::string pathForChild = fs::absolute(scriptPath).string();

    int workerPort = 0;
    if (desiredPort > 0) {
        workerPort = desiredPort; // restart: the task keeps its port and id
    } else {
        // Let the OS choose a bindable port, including on Windows where adjacent
        // ports can belong to an excluded range even when no process listens.
        workerPort = pickFreeLocalPort();
        if (workerPort <= 0) {
            std::cerr << "[debugger] no available local worker port" << std::endl;
            return {false, 0};
        }
    }

    ProcessSpec spec;
    spec.exe  = getExePath();
    spec.args = {"--run-worker", pathForChild};
    spec.env  = {{"CAMEL_DB_WORKER_PORT", std::to_string(workerPort)}};

    auto splitter  = std::make_shared<LineSplitter>();
    splitter->port = workerPort;
    // Set once the process is in the table; the exit callback removes exactly that entry (a
    // restart may already have put the next worker of the same port there).
    auto self = std::make_shared<std::weak_ptr<ChildProcess>>();
    ProcessCallbacks callbacks;
    callbacks.onStdout = [splitter](std::string_view chunk) { splitter->feed(chunk); };
    callbacks.onExit   = [splitter, workerPort, self](int, int) {
        splitter->flush();
        bool current = false;
        {
            std::lock_guard<std::mutex> lock(g_workerMutex);
            auto it = g_workers.find(workerPort);
            if (it != g_workers.end() && it->second == self->lock()) {
                g_workers.erase(it);
                current = true;
            }
        }
        if (current)
            setTaskState(workerPort, "exited");
    };

    std::string error;
    {
        // The table entry exists before the exit callback can look for it.
        std::lock_guard<std::mutex> lock(g_workerMutex);
        auto proc = ChildProcess::spawn(spec, std::move(callbacks), error);
        if (!proc) {
            std::cerr << "[debugger] " << error << std::endl;
            return {false, 0};
        }
        *self                 = proc;
        g_workers[workerPort] = std::move(proc);
    }
    if (desiredPort <= 0)
        registerTask(workerPort, scriptPath, sendRun ? "running" : "loaded");
    if (sendRun)
        getTaskState() = "running";
    // Return only when the worker's HTTP port accepts connections: a caller forwards to it at
    // once, and a failed forward would mark the new task as exited.
    {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        bool ready    = false;
        while (std::chrono::steady_clock::now() < deadline) {
            httplib::Client cli("127.0.0.1", workerPort);
            cli.set_connection_timeout(0, 100000);
            if (cli.Get("/api/state")) {
                ready = true;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!ready) {
            std::cerr << "[debugger] worker on port " << workerPort
                      << " did not become ready within 10s" << std::endl;
            terminateWorker(workerPort);
            return {false, 0};
        }
    }
    return {true, workerPort};
}

void terminateAllWorkers() { ChildProcess::terminateAll(); }

bool terminateWorker(int port) {
    std::shared_ptr<ChildProcess> proc;
    {
        std::lock_guard<std::mutex> lock(g_workerMutex);
        auto it = g_workers.find(port);
        if (it != g_workers.end())
            proc = it->second;
    }
    if (!proc)
        return false;
    proc->kill();
    // The caller respawns on the same port: wait until the old worker has released it.
    proc->waitExit(5000);
    return true;
}

} // namespace debugger
