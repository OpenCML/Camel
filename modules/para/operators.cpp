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
 * Created: Oct. 06, 2026
 * Updated: Oct. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "operators.h"
#include "camel/core/context/context.h"
#include "camel/core/error/runtime.h"
#include "camel/core/operator.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>

using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::rtdata;

namespace {

// One meeting point for the whole process.
struct Rendezvous {
    std::mutex mutex;
    std::condition_variable met;
    int64_t waiting    = 0; // calls blocked right now
    uint64_t round     = 0; // bumped every time a group forms
    int64_t lastGroup  = 0; // size of the group formed last
};

Rendezvous &rendezvous() {
    static Rendezvous instance;
    return instance;
}

} // namespace

std::unordered_map<std::string, operator_t> getParaOpsMap() {
    return {
        {"rendezvous", __para_rendezvous__},
    };
}

slot_t __para_rendezvous__(ArgsView &with, ArgsView &norm, Context &ctx) {
    const Int64 parties   = norm.get<Int64>(0);
    const Int64 timeoutMs = norm.get<Int64>(1);
    if (parties < 1 || timeoutMs < 0) {
        throwRuntimeFault(
            RuntimeDiag::RuntimeError,
            "<rendezvous> requires parties >= 1 and timeout_ms >= 0");
    }

    Rendezvous &point = rendezvous();
    std::unique_lock lock(point.mutex);
    ++point.waiting;
    if (point.waiting >= parties) {
        // This call completes a group: everyone waiting leaves with it.
        const int64_t group = point.waiting;
        point.lastGroup     = group;
        point.waiting       = 0;
        ++point.round;
        point.met.notify_all();
        return toSlot(static_cast<Int64>(group));
    }

    const uint64_t round = point.round;
    const bool met       = point.met.wait_for(lock, std::chrono::milliseconds(timeoutMs), [&] {
        return point.round != round;
    });
    if (met) {
        return toSlot(static_cast<Int64>(point.lastGroup));
    }
    // Timed out: report who was waiting with this call, and stop waiting.
    const int64_t together = point.waiting;
    --point.waiting;
    return toSlot(static_cast<Int64>(together));
}
