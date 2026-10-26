// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

bool deferredReleasePending() noexcept {
    return queue().pending.load(std::memory_order_acquire);
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