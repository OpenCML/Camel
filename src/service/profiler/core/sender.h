// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "buffer.h"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <thread>

class TraceSender {
  public:
    explicit TraceSender(TraceBuffer &buffer);
    ~TraceSender();

    void start();
    void stop();
    bool isRunning() const { return running_; }

  private:
    void run();

    TraceBuffer &buffer_;
    std::thread thread_;
    std::atomic<bool> running_;
};
