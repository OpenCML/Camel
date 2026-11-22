// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <functional>

namespace profiler {

class ScopeGuard {
  public:
    explicit ScopeGuard(std::function<void()> on_exit) : onExit_(std::move(on_exit)) {}

    ~ScopeGuard() {
        if (onExit_) {
            onExit_();
        }
    }

    // Disable copy
    ScopeGuard(const ScopeGuard &) = delete;
    ScopeGuard &operator=(const ScopeGuard &) = delete;

    // Enable move
    ScopeGuard(ScopeGuard &&other) noexcept : onExit_(std::move(other.onExit_)) {
        other.onExit_ = nullptr;
    }

    ScopeGuard &operator=(ScopeGuard &&other) noexcept {
        if (this != &other) {
            onExit_ = std::move(other.onExit_);
            other.onExit_ = nullptr;
        }
        return *this;
    }

  private:
    std::function<void()> onExit_;
};

} // namespace profiler
