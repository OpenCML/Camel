// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once
#include "event.h"

#include <mutex>
#include <queue>
#include <vector>

class TraceBuffer {
  public:
    void push(const TraceEvent &event);
    std::vector<TraceEvent> pop_all();

  private:
    std::mutex mutex_;
    std::queue<TraceEvent> queue_;
};
