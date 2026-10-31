// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Storage of type objects (see storage.h).
 */

#include "camel/core/type/storage.h"

#include "camel/core/mm.h"
#include "camel/core/type/intern.h"
#include "camel/utils/assert.h"

#include <array>
#include <atomic>
#include <cstdlib>
#include <iostream>
#include <mutex>

namespace camel::core::type {

namespace {

constexpr size_t kKindCount = static_cast<size_t>(TypeStorageKind::Count);

constexpr std::array<const char *, kKindCount> kKindNames = {
    "primitive",
    "array",
    "tuple",
    "struct",
    "function",
    "collection",
    "enum",
    "typevar",
    "params",
};

struct Counter {
    std::atomic<size_t> objects{0};
    std::atomic<size_t> bytes{0};
};

std::array<Counter, kKindCount> &counters() {
    static std::array<Counter, kKindCount> instance;
    return instance;
}

} // namespace

void enableTypeStatsReport() {
    static std::once_flag once;
    std::call_once(once, [] { std::atexit([] { reportTypeStorage(std::cerr); }); });
}

void *allocTypeStorage(TypeStorageKind kind, size_t size, size_t align) {
    void *mem = mm::permSpace().alloc(size, align);
    GUARD(mem != nullptr, "Failed to allocate type storage from the permanent space.");
    Counter &counter = counters()[static_cast<size_t>(kind)];
    counter.objects.fetch_add(1, std::memory_order_relaxed);
    counter.bytes.fetch_add(size, std::memory_order_relaxed);
    return mem;
}

void reportTypeStorage(std::ostream &os) {
    os << "[camel] types:";
    for (size_t i = 0; i < kKindCount; ++i) {
        const Counter &counter = counters()[i];
        os << ' ' << kKindNames[i] << '=' << counter.objects.load(std::memory_order_relaxed) << '/'
           << counter.bytes.load(std::memory_order_relaxed);
    }
    // The type table: ids handed out (interned types and primitive singletons) and how many
    // constructions it answered with an existing object instead of allocating.
    const TypeTable &table = TypeTable::instance();
    os << " interned=" << (table.size() - 1) << " intern_hits=" << table.hits();
    os << '\n';
}

} // namespace camel::core::type
