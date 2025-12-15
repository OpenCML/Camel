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
 * Created: Dec. 15, 2025
 * Updated: Dec. 15, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

// #define OPPERF_ENABLED

#include "builtin/passes/sched/common/bytecode.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace opperf {

#ifdef OPPERF_ENABLED

inline uint64_t rdtsc() noexcept {
    unsigned lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

struct OpcodeStat {
    std::atomic<uint64_t> count{0};
    std::atomic<uint64_t> total_cycles{0};
    std::unordered_map<std::string, OpcodeStat> substats;
};

class PerfMonitor {
  public:
    void start() {
        running_     = true;
        total_start_ = std::chrono::steady_clock::now();
    }

    void stop() {
        running_   = false;
        total_end_ = std::chrono::steady_clock::now();
    }

    void reset() {
        opstats_.clear();
        total_start_ = {};
        total_end_   = {};
    }

    void record(const OpCode &opcode, uint64_t cycles, const std::string &tag = "") {
        if (!running_)
            return;
        auto &stat = opstats_[opcode];
        stat.count++;
        stat.total_cycles += cycles;

        if (!tag.empty()) {
            auto &sub = stat.substats[tag];
            sub.count++;
            sub.total_cycles += cycles;
        }
    }

    void report(std::ostream &os) {
        double elapsed_sec = std::chrono::duration<double>(total_end_ - total_start_).count();
        uint64_t total_cycles_sum = total_cycles_all();

        if (elapsed_sec <= 0.0 || total_cycles_sum == 0) {
            os << "No performance data collected.\n";
            return;
        }

        double cycles_per_ns = total_cycles_sum / (elapsed_sec * 1e9);
        double ns_per_cycle  = 1.0 / cycles_per_ns;
        double total_ns_all  = elapsed_sec * 1e9; // 总运行时间纳秒

        os << "\n=========== FastVM Opcode Perf Report ===========\n";
        os << std::left << std::setw(15) << "Opcode" << std::right << std::setw(15) << "Count"
           << std::setw(20) << "Total Cycles" << std::setw(15) << "Avg Cycles" << std::setw(15)
           << "Avg(ns)" << std::setw(15) << "Total Time" << std::setw(10) << "Pct(%)"
           << "\n";
        os << std::string(105, '-') << "\n";

        std::vector<std::pair<const OpCode *, const OpcodeStat *>> sorted_ops;
        sorted_ops.reserve(opstats_.size());
        for (auto &kv : opstats_) {
            sorted_ops.emplace_back(&kv.first, &kv.second);
        }

        std::sort(sorted_ops.begin(), sorted_ops.end(), [](auto &a, auto &b) {
            return a.second->total_cycles > b.second->total_cycles;
        });

        uint64_t total_cycles = 0, total_count = 0;

        for (auto &[code_ptr, stat_ptr] : sorted_ops) {
            const auto &code = *code_ptr;
            const auto &stat = *stat_ptr;

            total_cycles += stat.total_cycles;
            total_count += stat.count;

            double avg_cycles = stat.count ? (double)stat.total_cycles / stat.count : 0.0;
            double avg_ns     = avg_cycles * ns_per_cycle;
            double total_ns   = stat.total_cycles * ns_per_cycle;
            double pct_total  = (total_ns / total_ns_all) * 100.0;

            os << std::left << std::setw(15) << to_string(code) << std::right << std::setw(15)
               << fmt_num(stat.count) << std::setw(20) << fmt_num(stat.total_cycles)
               << std::setw(15) << fmt_num((uint64_t)avg_cycles) << std::setw(15) << std::fixed
               << std::setprecision(3) << avg_ns << std::setw(15) << fmt_time_ns(total_ns)
               << std::setw(10) << std::fixed << std::setprecision(2) << pct_total << "\n";

            // === 子标签统计 ===
            std::vector<std::pair<const std::string *, const OpcodeStat *>> sorted_sub;
            sorted_sub.reserve(stat.substats.size());
            for (auto &kv : stat.substats) {
                sorted_sub.emplace_back(&kv.first, &kv.second);
            }

            std::sort(sorted_sub.begin(), sorted_sub.end(), [](auto &a, auto &b) {
                return a.second->total_cycles > b.second->total_cycles;
            });

            for (auto &[tag_ptr, sub_ptr] : sorted_sub) {
                const auto &tag = *tag_ptr;
                const auto &sub = *sub_ptr;

                double avg_cycles_sub = sub.count ? (double)sub.total_cycles / sub.count : 0.0;
                double avg_ns_sub     = avg_cycles_sub * ns_per_cycle;
                double total_ns_sub   = sub.total_cycles * ns_per_cycle;
                double pct_sub        = (total_ns_sub / total_ns_all) * 100.0;

                os << " -> " << std::left << std::setw(11) << tag << std::right << std::setw(15)
                   << fmt_num(sub.count) << std::setw(20) << fmt_num(sub.total_cycles)
                   << std::setw(15) << fmt_num((uint64_t)avg_cycles_sub) << std::setw(15)
                   << std::fixed << std::setprecision(3) << avg_ns_sub << std::setw(15)
                   << fmt_time_ns(total_ns_sub) << std::setw(10) << std::fixed
                   << std::setprecision(2) << pct_sub << "\n";
            }
        }

        os << std::string(105, '-') << "\n";
        double avg_cycles_total = total_count ? (double)total_cycles / total_count : 0.0;
        double avg_ns_total     = avg_cycles_total * ns_per_cycle;
        double total_ns_cycles  = total_cycles * ns_per_cycle;
        double pct_cycles_total = (total_ns_cycles / total_ns_all) * 100.0;

        os << std::left << std::setw(15) << "TOTAL" << std::right << std::setw(15)
           << fmt_num(total_count) << std::setw(20) << fmt_num(total_cycles) << std::setw(15)
           << fmt_num((uint64_t)avg_cycles_total) << std::setw(15) << std::fixed
           << std::setprecision(3) << avg_ns_total << std::setw(15) << fmt_time_ns(total_ns_cycles)
           << std::setw(10) << std::fixed << std::setprecision(2) << pct_cycles_total << "\n";

        os << "\nElapsed time: " << std::fixed << std::setprecision(6) << elapsed_sec << " s\n";
        os << "=================================================\n";
    }

    static PerfMonitor &instance() {
        static PerfMonitor inst;
        return inst;
    }

  private:
    bool running_ = false;
    std::chrono::steady_clock::time_point total_start_, total_end_;
    std::unordered_map<OpCode, OpcodeStat> opstats_;

    static std::string fmt_num(uint64_t num) {
        std::ostringstream oss;
        oss.imbue(std::locale(""));
        oss << std::fixed << num;
        return oss.str();
    }

    static std::string fmt_time_ns(double ns) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3);
        if (ns < 1e3)
            oss << ns << " ns";
        else if (ns < 1e6)
            oss << ns / 1e3 << " us";
        else if (ns < 1e9)
            oss << ns / 1e6 << " ms";
        else
            oss << ns / 1e9 << " s ";
        return oss.str();
    }

    uint64_t total_cycles_all() const {
        uint64_t total = 0;
        for (auto &kv : opstats_)
            total += kv.second.total_cycles;
        return total;
    }
};

// === 外部接口 ===

inline void start() { PerfMonitor::instance().start(); }
inline void stop() { PerfMonitor::instance().stop(); }
inline void reset() { PerfMonitor::instance().reset(); }
inline void report(std::ostream &os) { PerfMonitor::instance().report(os); }

struct ScopeTimer {
    OpCode opcode;
    std::string tag;
    uint64_t start;
    ScopeTimer(OpCode code, const std::string &t = "") : opcode(code), tag(t), start(rdtsc()) {}
    ~ScopeTimer() {
        uint64_t end = rdtsc();
        PerfMonitor::instance().record(opcode, end - start, tag);
    }
};

#else

inline void start() {}
inline void stop() {}
inline void reset() {}
inline void report(std::ostream &) {}

struct ScopeTimer {
    explicit ScopeTimer(OpCode code, const std::string &tag = "") {}
};

#endif // OPPERF_ENABLED

} // namespace opperf
