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
 * Updated: Feb. 23, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>

namespace debugger {

class MemMonitor {
public:
    MemMonitor();
    ~MemMonitor();

    // Start monitor (background thread + HTTP server)
    void start(int port = 8765);
    void stop();

    bool isRunning() const { return running_; }
    int  port() const { return port_; }

    /// Enable step-on-alloc: pause after each GC allocation, wait for Web UI continue
    void enableAllocStep(bool enable) { allocStepEnabled_ = enable; }
    bool allocStepEnabled() const { return allocStepEnabled_; }

    /// Called from post-alloc hook: take snapshot, store last alloc, then block until /api/continue
    void pauseAndWaitForContinue(const void *ptr, size_t size, const char *space);

private:
    void scanThreadLoop();
    void httpServerLoop();

    std::atomic<bool> running_{false};
    std::atomic<bool> scanStop_{false};
    std::atomic<bool> httpStop_{false};
    std::atomic<bool> allocStepEnabled_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> continueRequested_{false};
    int               port_ = 8765;

    std::mutex              continueMutex_;
    std::condition_variable continueCond_;

    std::string       latestJson_;
    std::string       lastAllocJson_;  // 最近一次分配事件的 JSON
    std::mutex        jsonMutex_;

    std::atomic<void *> serverPtr_{nullptr}; // httplib::Server*, used to stop listening on stop
    std::thread        scanThread_;
    std::thread        httpThread_;
};

} // namespace debugger
