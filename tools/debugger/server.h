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
 * Updated: Mar. 06, 2026
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
 *   getState_/getGirJson_ callbacks or local data directly.
 * - Worker processes set a WorkerRunHandler so POST /api/run drives the local
 *   interpreter instead of spawning another child.
 *
 * Member layout is split into three groups:
 *   1. Shared       - used by both Gateway and Worker
 *   2. Gateway-only - child-port management and request forwarding
 *   3. Worker-only  - memory scan, breakpoints, pause/continue, run errors
 */

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

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

    using GetStateFn   = std::function<std::string()>;
    using GetGirJsonFn = std::function<std::pair<std::string, std::string>(
        const std::string &path, const std::string &graphId)>;
    void setQueryCallbacks(GetStateFn getState, GetGirJsonFn getGirJson);

    // =========================================================================
    // Gateway-only — child-port tracking and HTTP forwarding (parent process)
    // =========================================================================

    void setChildPort(int port); /// Record the most-recently spawned worker port.
    void clearChildPort();
    int getChildPort() const { return childPort_.load(); }
    bool forwardPostToChild(const std::string &apiPath, const std::string &body = "");
    /// Forward POST to a specific port (used by Commands after parent executes; does not modify
    /// childPort_).
    bool forwardPostToPort(int port, const std::string &apiPath, const std::string &body = "");

    // =========================================================================
    // Worker-only — memory scan, breakpoints, script execution control
    // =========================================================================

    using WorkerRunHandlerFn =
        std::function<void(const std::string &body, std::string &responseBody)>;
    void setWorkerRunHandler(WorkerRunHandlerFn fn);
    bool isWorkerProcess() const { return static_cast<bool>(workerRunHandler_); }

    void startMemoryScan(); /// Start background memory scan thread. Worker-only.
    void stopMemoryScan();  /// Stop memory scan thread. Worker-only.
    bool isMemoryScanRunning() const { return memoryScanRunning_; }

    void pauseAndWaitForContinue(const void *ptr, size_t size, const char *space); /// Worker-only.
    void pauseAndWaitForGirBreakpoint(
        const std::string &nodeId,
        const std::string &graphId);                        /// Worker-only.
    void pauseAndWaitForPipelineStage(const char *stageId); /// Worker-only.
    bool isPaused() const { return paused_.load(); }
    void requestContinue();  /// Worker-only.
    void requestRestart();   /// Worker-only.
    void requestTerminate(); /// Worker-only.

    void enableAllocStep(bool enable) { allocStepEnabled_ = enable; }
    bool allocStepEnabled() const { return allocStepEnabled_; }
    void setAllocBreakSpaces(std::unordered_set<std::string> spaces);
    /// 当前断点配置（分配空间型）；Run/Restart 时随统一断点状态一并推送。
    std::unordered_set<std::string> getAllocBreakSpaces();
    void clearLastRunError();

    void setGirBreakpointNodeIds(std::unordered_set<uintptr_t> ids);
    void setGirBreakpointNodeIdsFromStrings(const std::vector<std::string> &nodeIds);
    bool isGirBreakpointNode(uintptr_t nodePtr) const;
    bool isGirBreakpointNodeStable(const std::string &stableId) const;
    std::unordered_set<uintptr_t> getGirBreakpointNodeIds() const;
    std::vector<std::string> getGirBreakpointNodeIdsForApi() const;

    /// 父进程保留的断点类型列表（enabled）；Run/Restart 时推送给 worker。
    void setEnabledBreakpointTypes(std::vector<std::string> types);
    std::vector<std::string> getEnabledBreakpointTypes() const;

    /// 父进程保留的上次 run 请求 body（含 passes）；Restart 时转发给新 worker。
    void setLastRunBody(std::string body);
    std::string getLastRunBody() const;

  private:
    // =========================================================================
    // Shared state
    // =========================================================================
    void httpServerLoop();
    std::atomic<bool> running_{false};
    std::atomic<bool> httpStop_{false};
    int port_ = 8765;
    GetStateFn getState_;
    GetGirJsonFn getGirJson_;
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
    std::unordered_set<uintptr_t> girBreakpointNodeIds_;
    std::unordered_set<std::string> girBreakpointStableIds_;
    mutable std::mutex girBreakpointNodeIdsMutex_;
    std::vector<std::string> enabledBreakpointTypes_;
    mutable std::mutex enabledBreakpointTypesMutex_;
    std::string lastRunBody_;
    mutable std::mutex lastRunBodyMutex_;
    std::thread scanThread_;
};

} // namespace debugger
