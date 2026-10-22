// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "buffer.h"

void TraceBuffer::push(const TraceEvent &event) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(event);
}

std::vector<TraceEvent> TraceBuffer::pop_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TraceEvent> result;
    while (!queue_.empty()) {
        result.push_back(queue_.front());
        queue_.pop();
    }
    return result;
}
