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
 * Created: Oct. 27, 2025
 * Updated: Oct. 27, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#define MEMPERF_ENABLED

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace memperf {

#ifndef MEMPERF_ENABLED
inline void start() {}
inline void stop() {}
inline void reset() {}
inline void report(std::ostream &) {}
inline void enable_logging(bool) {}
#else

// 分配尺寸桶（统计不同尺寸范围的分配次数）
static constexpr size_t BUCKET_COUNT = 8; // 8个桶
// 尺寸范围：<16, <64, <256, <1024, <4096, <16384, <65536, >=65536
static constexpr std::array<size_t, BUCKET_COUNT> bucket_limits =
    {16, 64, 256, 1024, 4096, 16384, 65536, SIZE_MAX};

struct Stats {
    std::atomic<size_t> start_cycles{0};
    std::atomic<size_t> total_cycles{0};
    std::atomic<size_t> alloc_count{0};
    std::atomic<size_t> free_count{0};
    std::atomic<size_t> alloc_size_total{0};
    std::atomic<size_t> free_size_total{0};
    std::atomic<uint64_t> alloc_cycles_total{0};
    std::atomic<uint64_t> free_cycles_total{0};
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point end_time;

    std::array<std::atomic<size_t>, BUCKET_COUNT> alloc_size_buckets{};
};

class PerfMonitor {
  public:
    void start();
    void stop();
    void reset();
    void report(std::ostream &os);
    void record_alloc(size_t size, uint64_t cycles);
    void record_free(size_t size, uint64_t cycles);
    void enable_logging(bool e);

  private:
    bool running = false;
    bool logging_enabled = false;
    Stats stats;

    size_t bucket_index(size_t size);
};

// 全局实例
PerfMonitor &instance();

// 全局 API
inline void start() { instance().start(); }
inline void stop() { instance().stop(); }
inline void reset() { instance().reset(); }
inline void report(std::ostream &os) { instance().report(os); }
inline void enable_logging(bool e) { instance().enable_logging(e); }

// 高精度计时
inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

#endif // NDEBUG
} // namespace memperf
