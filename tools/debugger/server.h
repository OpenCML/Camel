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
#pragma once

/**
 * @file server.h
 * @brief HTTP API server and runtime control for the debugger.
 *
 * Design:
 * - Both parent (Gateway) and worker processes instantiate this class.
 *   `isWorkerProcess()` distinguishes the two roles at runtime.
 * - State-changing operations (continue/restart/terminate, settings, breakpoint-types)
 *   go through CommandDispatcher; queries (state/snapshot/step-paused) use
 *   getState_/getGirDot_ callbacks or local data directly.
 * - Worker processes set a WorkerRunHandler so POST /api/run drives the local
 *   interpreter instead of spawning another child.
 *
 * Member layout is split into three groups:
 *   1. Shared       - used by both Gateway and Worker
 *   2. Gateway-only - child-port management and request forwarding
 *   3. Worker-only  - memory scan, alloc breakpoints, pause/continue, run errors
 */

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>

namespace debugger {

struct RestartRequestedException : std::exception {
    const char *what() const noexcept override { return "restart requested"; }
};
struct TerminateRequestedException : std::exception {
    const char *what() const noexcept override { return "terminate requested"; }
};

class DebuggerServer {
  public:
    DebuggerServer();
    ~DebuggerServer();

    // =========================================================================
    // Shared — used by both Gateway (parent) and Worker (child)
    // =========================================================================

    void start(int port = 8765, bool silent = false);
    void stop();
    bool isRunning() const { return running_; }
    int port() const { return port_; }

    using GetStateFn  = std::function<std::string()>;
    using GetGirDotFn = std::function<std::pair<std::string, std::string>(const std::string &path)>;
    void setQueryCallbacks(GetStateFn getState, GetGirDotFn getGirDot);

    // =========================================================================
    // Gateway-only — child-port tracking and HTTP forwarding (parent process)
    // =========================================================================

    void setChildPort(int port); /// Record the most-recently spawned worker port.
    void clearChildPort();
    int getChildPort() const { return childPort_.load(); }
    bool forwardPostToChild(const std::string &apiPath, const std::string &body = "");

    // =========================================================================
    // Worker-only — memory scan, alloc breakpoints, script execution control
    // =========================================================================

    using WorkerRunHandlerFn =
        std::function<void(const std::string &body, std::string &responseBody)>;
    void setWorkerRunHandler(WorkerRunHandlerFn fn);
    bool isWorkerProcess() const { return static_cast<bool>(workerRunHandler_); }

    void startMemoryScan(); /// Start background memory scan thread. Worker-only.
    void stopMemoryScan();  /// Stop memory scan thread. Worker-only.
    bool isMemoryScanRunning() const { return memoryScanRunning_; }

    void pauseAndWaitForContinue(const void *ptr, size_t size, const char *space); /// Worker-only.
    bool isPaused() const { return paused_.load(); }
    void requestContinue();  /// Worker-only.
    void requestRestart();   /// Worker-only.
    void requestTerminate(); /// Worker-only.

    void enableAllocStep(bool enable) { allocStepEnabled_ = enable; }
    bool allocStepEnabled() const { return allocStepEnabled_; }
    void setAllocBreakSpaces(std::unordered_set<std::string> spaces);
    void clearLastRunError();

  private:
    // =========================================================================
    // Shared state
    // =========================================================================
    void httpServerLoop();
    std::atomic<bool> running_{false};
    std::atomic<bool> httpStop_{false};
    int port_ = 8765;
    GetStateFn getState_;
    GetGirDotFn getGirDot_;
    std::atomic<void *> serverPtr_{nullptr};
    std::thread httpThread_;
    class LogSink;
    std::unique_ptr<LogSink> logSink_;
    size_t logStreamHandle_ = 0;

    // =========================================================================
    // Gateway-only state
    // =========================================================================
    std::atomic<int> childPort_{0};
    WorkerRunHandlerFn workerRunHandler_;

    // =========================================================================
    // Worker-only state — only meaningful when isWorkerProcess() == true
    // =========================================================================
    void scanThreadLoop();
    std::atomic<bool> scanStop_{true};
    std::atomic<bool> memoryScanRunning_{false};
    std::atomic<bool> allocStepEnabled_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> continueRequested_{false};
    std::atomic<bool> restartRequested_{false};
    std::atomic<bool> terminateRequested_{false};
    std::mutex continueMutex_;
    std::condition_variable continueCond_;
    std::string latestJson_;
    std::string lastAllocJson_;
    std::mutex jsonMutex_;
    std::string lastRunError_;
    std::string lastRunErrorExpression_;
    std::string lastRunErrorFile_;
    int lastRunErrorLine_ = 0;
    std::mutex lastRunErrorMutex_;
    std::unordered_set<std::string> allocBreakSpaces_;
    std::mutex allocBreakSpacesMutex_;
    std::thread scanThread_;
};

} // namespace debugger
