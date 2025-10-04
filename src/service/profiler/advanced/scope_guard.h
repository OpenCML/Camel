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
 * Created: Aug. 20, 2025
 * Updated: Oct. 04, 2025
 * Supported by: National Key Research and Development Program of China
 */

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
