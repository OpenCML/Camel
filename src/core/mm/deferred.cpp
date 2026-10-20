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
 * Author: Camel Contributors
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/mm/deferred.h"

#include <atomic>
#include <mutex>
#include <vector>

namespace camel::core::mm {

namespace {

struct Entry {
    DeferredRelease release = nullptr;
    void *context           = nullptr;
};

struct Queue {
    std::mutex mutex;
    std::vector<Entry> entries;
    std::atomic<bool> pending{false};
};

// Leaked on purpose: finalizers may run while static objects are being destroyed.
Queue &queue() {
    static auto *value = new Queue();
    return *value;
}

} // namespace

void deferRelease(DeferredRelease release, void *context) noexcept {
    if (!release) {
        return;
    }
    Queue &q = queue();
    try {
        std::lock_guard<std::mutex> lock(q.mutex);
        q.entries.push_back(Entry{release, context});
        q.pending.store(true, std::memory_order_release);
    } catch (...) {
        // No memory for the queue entry: release in place rather than leak.
        release(context);
    }
}

size_t runDeferredReleases() noexcept {
    Queue &q = queue();
    if (!q.pending.load(std::memory_order_acquire)) {
        return 0;
    }
    std::vector<Entry> entries;
    {
        std::lock_guard<std::mutex> lock(q.mutex);
        entries.swap(q.entries);
        q.pending.store(false, std::memory_order_release);
    }
    for (const Entry &entry : entries) {
        entry.release(entry.context);
    }
    return entries.size();
}

} // namespace camel::core::mm