/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * Updated: Feb. 24, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace debugger {

/// Thrown from pauseAndWaitForContinue when Web UI requests restart (clean run from scratch).
struct RestartRequestedException : std::exception {
    const char *what() const noexcept override { return "restart requested"; }
};

/// Debugger API server (serve). Provides HTTP API for Web UI; optional memory scan / alloc-step.
class DebuggerServer {
  public:
    DebuggerServer();
    ~DebuggerServer();

    /// Start API server (serve command). Does not start memory scan.
    void start(int port = 8765);
    void stop();

    /// With server running: start/stop memory scan (used when run with memory monitor option).
    void startMemoryScan();
    void stopMemoryScan();

    bool isRunning() const { return running_; }
    bool isMemoryScanRunning() const { return memoryScanRunning_; }
    int port() const { return port_; }

    void enableAllocStep(bool enable) { allocStepEnabled_ = enable; }
    bool allocStepEnabled() const { return allocStepEnabled_; }

    /// Post-alloc hook: snapshot, store last alloc, block until /api/continue or /api/restart.
    void pauseAndWaitForContinue(const void *ptr, size_t size, const char *space);

    /// Request restart from Web UI: wake paused run and throw RestartRequestedException so main can
    /// clean and re-run.
    void requestRestart();

    /// Injected by main: state / file / run / settings.
    using GetStateFn    = std::function<std::string()>;
    using LoadFileFn    = std::function<bool(const std::string &path)>;
    using RunWithOptsFn = std::function<bool(bool memoryMonitor, bool allocStep)>;
    using SetSettingsFn = std::function<void(bool verbose, const std::string &logFile)>;
    void setDebuggerCallbacks(
        GetStateFn getState, LoadFileFn loadFile, RunWithOptsFn runWithOpts,
        SetSettingsFn setSettings = nullptr);

  private:
    void scanThreadLoop();
    void httpServerLoop();

    std::atomic<bool> running_{false};
    std::atomic<bool> scanStop_{true};
    std::atomic<bool> memoryScanRunning_{false};
    std::atomic<bool> httpStop_{false};
    std::atomic<bool> allocStepEnabled_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> continueRequested_{false};
    std::atomic<bool> restartRequested_{false};
    int port_ = 8765;

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

    GetStateFn getState_;
    LoadFileFn loadFile_;
    RunWithOptsFn runWithOpts_;
    SetSettingsFn setSettings_;

    std::atomic<void *> serverPtr_{nullptr};
    std::thread scanThread_;
    std::thread httpThread_;

    class LogSink;
    std::unique_ptr<LogSink> logSink_;
    size_t logStreamHandle_ = 0;
};

} // namespace debugger
