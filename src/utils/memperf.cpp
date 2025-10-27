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

#include "memperf.h"

#include <cstdlib>
#include <iomanip>
#include <locale>
#include <sstream>

#ifdef MEMPERF_ENABLED
namespace memperf {

PerfMonitor &instance() {
    static PerfMonitor inst;
    return inst;
}

void PerfMonitor::start() {
    running = true;
    stats.start_cycles = rdtsc();
    stats.start_time = std::chrono::steady_clock::now();
}

void PerfMonitor::stop() {
    running = false;
    uint64_t end_cycles = rdtsc();
    stats.end_time = std::chrono::steady_clock::now();
    stats.total_cycles += (end_cycles - stats.start_cycles);
}

void PerfMonitor::reset() {
    stats.alloc_count.store(0);
    stats.free_count.store(0);
    stats.alloc_size_total.store(0);
    stats.free_size_total.store(0);
    stats.alloc_cycles_total.store(0);
    stats.free_cycles_total.store(0);
    for (auto &b : stats.alloc_size_buckets) {
        b.store(0);
    }
}

static std::string format_with_commas(uint64_t value) {
    std::stringstream ss;
    ss.imbue(std::locale("")); // 使用系统本地千位分隔
    ss << std::fixed << value;
    return ss.str();
}

void PerfMonitor::report(std::ostream &os) {
    const int label_width = 30; // 标签列宽
    const int value_width = 20; // 数值列宽

    os << "=============== Memory Perf Report ===============\n";

    // 主统计信息
    os << std::left << std::setw(label_width) << "Alloc count:" << std::right
       << std::setw(value_width) << format_with_commas(stats.alloc_count) << "\n";

    os << std::left << std::setw(label_width) << "Alloc total size:" << std::right
       << std::setw(value_width) << (format_with_commas(stats.alloc_size_total)) << " bytes"
       << "\n";

    os << std::left << std::setw(label_width) << "Alloc total cycles:" << std::right
       << std::setw(value_width) << format_with_commas(stats.alloc_cycles_total) << "\n";

    os << std::left << std::setw(label_width) << "Avg alloc cycles:" << std::right
       << std::setw(value_width)
       << (stats.alloc_count ? stats.alloc_cycles_total / stats.alloc_count : 0) << "\n";

    os << std::left << std::setw(label_width) << "Free count:" << std::right
       << std::setw(value_width) << format_with_commas(stats.free_count) << "\n";

    os << std::left << std::setw(label_width) << "Free total size:" << std::right
       << std::setw(value_width) << (format_with_commas(stats.free_size_total)) << " bytes" << "\n";

    os << std::left << std::setw(label_width) << "Free total cycles:" << std::right
       << std::setw(value_width) << format_with_commas(stats.free_cycles_total) << "\n";

    os << std::left << std::setw(label_width) << "Avg free cycles:" << std::right
       << std::setw(value_width)
       << (stats.free_count ? stats.free_cycles_total / stats.free_count : 0) << "\n";

    os << std::left << std::setw(label_width) << "Total cycles:" << std::right
       << std::setw(value_width)
       << format_with_commas(stats.alloc_cycles_total.load() + stats.free_cycles_total.load())
       << "\n";

    // 计算总运行时间（秒）
    double elapsed_seconds =
        std::chrono::duration<double>(stats.end_time - stats.start_time).count();
    double seconds_per_cycle = (elapsed_seconds / static_cast<double>(stats.total_cycles));
    double ns_per_cycle = seconds_per_cycle * 1e9;

    os << std::fixed << std::setprecision(4);

    os << "\n"
       << std::left << std::setw(label_width) << "Run time (start->stop):" << std::right
       << std::setw(value_width) << elapsed_seconds << " s\n";

    os << std::left << std::setw(label_width) << "Estimated time per cycle:" << std::right
       << std::setw(value_width) << ns_per_cycle << " ns\n";

    if (stats.alloc_count > 0) {
        os << std::left << std::setw(label_width) << "Avg alloc time:" << std::right
           << std::setw(value_width)
           << (stats.alloc_cycles_total / static_cast<double>(stats.alloc_count)) * ns_per_cycle
           << " ns\n";
    }
    if (stats.free_count > 0) {
        os << std::left << std::setw(label_width) << "Avg free time:" << std::right
           << std::setw(value_width)
           << (stats.free_cycles_total / static_cast<double>(stats.free_count)) * ns_per_cycle
           << " ns\n";
    }

    // total alloc time and free time estimation
    os << std::left << std::setw(label_width) << "Total alloc time:" << std::right
       << std::setw(value_width) << (stats.alloc_cycles_total.load() * ns_per_cycle) / 1e9
       << " s\n";
    os << std::left << std::setw(label_width) << "Total free time:" << std::right
       << std::setw(value_width) << (stats.free_cycles_total.load() * ns_per_cycle) / 1e9 << " s\n";
    //  total alloc + free time
    os << std::left << std::setw(label_width) << "Total alloc + free time:" << std::right
       << std::setw(value_width)
       << ((stats.alloc_cycles_total.load() + stats.free_cycles_total.load()) * ns_per_cycle) / 1e9
       << " s\n";

    os.unsetf(std::ios::fixed);
    os << std::setprecision(6); // 恢复到默认精度

    // 分配大小分布
    os << "\nAlloc size distribution:\n";
    for (size_t i = 0; i < BUCKET_COUNT; ++i) {
        std::string label;
        if (i == BUCKET_COUNT - 1)
            label = "< MAX  bytes:";
        else {
            std::ostringstream lbl;
            lbl << "<" << bucket_limits[i] << " bytes:";
            label = lbl.str();
        }

        os << std::left << std::setw(label_width) << label << std::right << std::setw(value_width)
           << format_with_commas(stats.alloc_size_buckets[i]) << "\n";
    }
}

void PerfMonitor::record_alloc(size_t size, uint64_t cycles) {
    if (!running)
        return;
    stats.alloc_count++;
    stats.alloc_size_total += size;
    stats.alloc_cycles_total += cycles;
    stats.alloc_size_buckets[bucket_index(size)]++;

    if (logging_enabled) {
        std::cout << "[ALLOC] size=" << size << " cycles=" << cycles << "\n";
    }
}

void PerfMonitor::record_free(size_t size, uint64_t cycles) {
    if (!running)
        return;
    stats.free_count++;
    stats.free_size_total += size;
    stats.free_cycles_total += cycles;

    if (logging_enabled) {
        std::cout << "[FREE] size=" << size << " cycles=" << cycles << "\n";
    }
}

void PerfMonitor::enable_logging(bool e) { logging_enabled = e; }

size_t PerfMonitor::bucket_index(size_t size) {
    for (size_t i = 0; i < BUCKET_COUNT; ++i) {
        if (size < bucket_limits[i])
            return i;
    }
    return BUCKET_COUNT - 1;
}

} // namespace memperf

// 拦截全局 new/delete
void *operator new(std::size_t size) {
    auto start = memperf::rdtsc();
    void *p = std::malloc(size);
    auto end = memperf::rdtsc();
    memperf::instance().record_alloc(size, end - start);
    if (!p)
        throw std::bad_alloc();
    return p;
}

void operator delete(void *p) noexcept {
    auto start = memperf::rdtsc();
    std::free(p);
    auto end = memperf::rdtsc();
    // 释放时无法直接得知原大小，传0
    memperf::instance().record_free(0, end - start);
}

void *operator new[](std::size_t size) {
    auto start = memperf::rdtsc();
    void *p = std::malloc(size);
    auto end = memperf::rdtsc();
    memperf::instance().record_alloc(size, end - start);
    if (!p)
        throw std::bad_alloc();
    return p;
}

void operator delete[](void *p) noexcept {
    auto start = memperf::rdtsc();
    std::free(p);
    auto end = memperf::rdtsc();
    memperf::instance().record_free(0, end - start);
}

#endif // NDEBUG
