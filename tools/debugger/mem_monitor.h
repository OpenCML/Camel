/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 *
 * Author: Zhenjie Wei
 * Created: Feb. 22, 2026
 * Updated: Feb. 23, 2026
 * Memory monitor: periodic GC region scan, HTTP server + Web UI
 */

#pragma once

#include <atomic>
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

private:
    void scanThreadLoop();
    void httpServerLoop();

    std::atomic<bool> running_{false};
    std::atomic<bool> scanStop_{false};
    std::atomic<bool> httpStop_{false};
    int               port_ = 8765;

    std::string       latestJson_;
    std::mutex        jsonMutex_;

    std::atomic<void *> serverPtr_{nullptr}; // httplib::Server*, used to stop listening on stop
    std::thread        scanThread_;
    std::thread        httpThread_;
};

} // namespace debugger
