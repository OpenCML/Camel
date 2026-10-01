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
 * Author: Camel Contributors
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "op_stats.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <utility>
#include <vector>

namespace camel::tensor::ops {

namespace {

// Kernels are plain function pointers, so each timed operator gets its own trampoline, instantiated
// from a fixed pool.
constexpr size_t kSlots = 512;

struct Slot {
    std::string name;
    operator_t kernel = nullptr;
    std::atomic<uint64_t> calls{0};
    std::atomic<uint64_t> nanos{0};
};

std::array<Slot, kSlots> &slots() {
    static std::array<Slot, kSlots> table;
    return table;
}

std::atomic<size_t> used{0};

template <size_t I> slot_t trampoline(ArgsView &with, ArgsView &norm, context::Context &ctx) {
    Slot &slot = slots()[I];
    const auto start = std::chrono::steady_clock::now();
    slot_t result = slot.kernel(with, norm, ctx);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    slot.calls.fetch_add(1, std::memory_order_relaxed);
    slot.nanos.fetch_add(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count(),
        std::memory_order_relaxed);
    return result;
}

template <size_t... I> constexpr std::array<operator_t, kSlots> makeTrampolines(std::index_sequence<I...>) {
    return {&trampoline<I>...};
}

constexpr std::array<operator_t, kSlots> kTrampolines = makeTrampolines(std::make_index_sequence<kSlots>{});

void report() {
    const size_t count = std::min(used.load(), kSlots);
    std::vector<const Slot *> rows;
    uint64_t total = 0;
    for (size_t i = 0; i < count; ++i) {
        if (slots()[i].calls.load() > 0) {
            rows.push_back(&slots()[i]);
            total += slots()[i].nanos.load();
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Slot *a, const Slot *b) {
        return a->nanos.load() > b->nanos.load();
    });
    std::fprintf(stderr, "tensor op stats: %.3f ms in kernels\n", total / 1e6);
    std::fprintf(stderr, "  %-24s %10s %12s %10s %6s\n", "operator", "calls", "total ms", "us/call", "%");
    for (const Slot *row : rows) {
        const double ms = row->nanos.load() / 1e6;
        std::fprintf(
            stderr, "  %-24s %10llu %12.3f %10.2f %6.1f\n", row->name.c_str(),
            static_cast<unsigned long long>(row->calls.load()), ms,
            ms * 1000.0 / static_cast<double>(row->calls.load()),
            total ? 100.0 * static_cast<double>(row->nanos.load()) / static_cast<double>(total) : 0.0);
    }
}

} // namespace

bool opStatsEnabled() {
    static const bool enabled = std::getenv("CAMEL_TENSOR_OPSTATS") != nullptr;
    return enabled;
}

operator_t timedKernel(const std::string &name, operator_t kernel) {
    static std::once_flag registered;
    std::call_once(registered, [] { std::atexit(report); });
    const size_t index = used.fetch_add(1);
    if (index >= kSlots) {
        return kernel;
    }
    slots()[index].name = name;
    slots()[index].kernel = kernel;
    return kTrampolines[index];
}

} // namespace camel::tensor::ops
