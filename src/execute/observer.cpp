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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/execute/observer.h"

#include <atomic>

namespace camel::execute {

namespace {
// Constant-initialised: no dynamic initialisation, so nothing runs when the library loads.
std::atomic<ExecutionObserver *> gObserver{nullptr};
} // namespace

ExecutionObserver *executionObserver() noexcept {
    return gObserver.load(std::memory_order_relaxed);
}

void setExecutionObserver(ExecutionObserver *observer) noexcept {
    gObserver.store(observer, std::memory_order_release);
}

} // namespace camel::execute
