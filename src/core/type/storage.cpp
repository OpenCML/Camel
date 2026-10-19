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
 * Created: Oct. 13, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
    "union",
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

/// Arranges for the report to be printed at exit, once, when CAMEL_TYPE_STATS is set to
/// anything but "0" or the empty string.
void installReportOnce() {
    static std::once_flag once;
    std::call_once(once, [] {
        const char *value = std::getenv("CAMEL_TYPE_STATS");
        if (value != nullptr && value[0] != '\0' && !(value[0] == '0' && value[1] == '\0')) {
            std::atexit([] { reportTypeStorage(std::cerr); });
        }
    });
}

} // namespace

void *allocTypeStorage(TypeStorageKind kind, size_t size, size_t align) {
    installReportOnce();
    void *mem = mm::permSpace().alloc(size, align);
    ASSERT(mem != nullptr, "Failed to allocate type storage from the permanent space.");
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
