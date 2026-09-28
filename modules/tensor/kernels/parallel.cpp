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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Worker pool behind `parallelFor`.
 *
 * A parallel region publishes a job (body, chunk count) under a new
 * generation number, runs chunks on the calling thread, and waits until every
 * chunk has completed. Workers claim chunks one at a time, so a slow worker
 * never blocks others.
 *
 * Tensor programs issue many short regions back to back, so both sides spin
 * briefly before blocking: an idle worker polls the generation counter, and the
 * caller polls the completion count, for about kSpinIterations pause
 * instructions before falling back to the condition variables.
 */

#include "parallel.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace camel::tensor::kernels {

namespace {

constexpr int kSpinIterations = 4096;

inline void cpuRelax() {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_pause();
#else
    std::this_thread::yield();
#endif
}

// True while the current thread executes a chunk of a parallel region. Nested
// regions then run serially without touching the pool (and its mutexes).
thread_local bool tInsideRegion = false;

struct RegionScope {
    bool previous;
    RegionScope() : previous(tInsideRegion) { tInsideRegion = true; }
    ~RegionScope() { tInsideRegion = previous; }
};

int defaultThreadCount() {
    if (const char *env = std::getenv("CAMEL_NUM_THREADS")) {
        const int requested = std::atoi(env);
        if (requested > 0) {
            return requested;
        }
    }
    const unsigned hw = std::thread::hardware_concurrency();
    return hw == 0 ? 1 : static_cast<int>(hw);
}

class WorkerPool {
  public:
    WorkerPool() { resize(defaultThreadCount()); }

    ~WorkerPool() { stopWorkers(); }

    int threads() const { return threadCount_; }

    void resize(int threads) {
        threads = std::max(threads, 1);
        std::lock_guard regionGuard(regionMutex_);
        stopWorkers();
        threadCount_ = threads;
        stopping_.store(false);
        for (int i = 1; i < threadCount_; ++i) {
            workers_.emplace_back([this] { workerLoop(); });
        }
    }

    void run(int64_t count, int64_t grain, const RangeFn &fn) {
        const int64_t maxChunks = std::max<int64_t>(1, count / std::max<int64_t>(grain, 1));
        const int64_t chunks    = std::min<int64_t>(threadCount_, maxChunks);
        if (chunks <= 1 || tInsideRegion) {
            fn(0, count);
            return;
        }
        // A busy pool means another region is active: run serially instead of waiting.
        std::unique_lock regionGuard(regionMutex_, std::try_to_lock);
        if (!regionGuard.owns_lock()) {
            fn(0, count);
            return;
        }

        {
            std::lock_guard guard(stateMutex_);
            body_       = &fn;
            count_      = count;
            chunkCount_ = chunks;
            nextChunk_  = 0;
            doneChunks_ = 0;
            doneCount_.store(0, std::memory_order_relaxed);
            firstError_ = nullptr;
            generation_.fetch_add(1, std::memory_order_release);
        }
        wake_.notify_all();

        runChunks();

        for (int spin = 0; spin < kSpinIterations; ++spin) {
            if (doneCount_.load(std::memory_order_acquire) == chunks) {
                break;
            }
            cpuRelax();
        }
        std::unique_lock guard(stateMutex_);
        finished_.wait(guard, [this] { return doneChunks_ == chunkCount_; });
        body_ = nullptr;
        if (firstError_) {
            std::rethrow_exception(firstError_);
        }
    }

  private:
    void workerLoop() {
        uint64_t seenGeneration = 0;
        for (;;) {
            for (int spin = 0; spin < kSpinIterations; ++spin) {
                if (generation_.load(std::memory_order_acquire) != seenGeneration ||
                    stopping_.load(std::memory_order_acquire)) {
                    break;
                }
                cpuRelax();
            }
            {
                std::unique_lock guard(stateMutex_);
                wake_.wait(guard, [&] {
                    return stopping_.load() || generation_.load() != seenGeneration;
                });
                if (stopping_.load()) {
                    return;
                }
                seenGeneration = generation_.load();
            }
            runChunks();
        }
    }

    // Claims and executes chunks until none remain. Called by workers and the caller.
    void runChunks() {
        for (;;) {
            int64_t chunk;
            const RangeFn *body;
            int64_t count, chunkCount;
            {
                std::lock_guard guard(stateMutex_);
                if (!body_ || nextChunk_ >= chunkCount_) {
                    return;
                }
                chunk      = nextChunk_++;
                body       = body_;
                count      = count_;
                chunkCount = chunkCount_;
            }
            const int64_t begin = count * chunk / chunkCount;
            const int64_t end   = count * (chunk + 1) / chunkCount;
            std::exception_ptr error;
            try {
                RegionScope scope;
                (*body)(begin, end);
            } catch (...) {
                error = std::current_exception();
            }
            {
                std::lock_guard guard(stateMutex_);
                if (error && !firstError_) {
                    firstError_ = error;
                }
                doneChunks_ += 1;
                doneCount_.store(doneChunks_, std::memory_order_release);
                if (doneChunks_ == chunkCount_) {
                    finished_.notify_all();
                }
            }
        }
    }

    void stopWorkers() {
        {
            std::lock_guard guard(stateMutex_);
            stopping_.store(true);
        }
        wake_.notify_all();
        for (auto &worker : workers_) {
            worker.join();
        }
        workers_.clear();
    }

    std::mutex regionMutex_; // held for the duration of one parallel region
    std::mutex stateMutex_;  // guards the job fields below
    std::condition_variable wake_;
    std::condition_variable finished_;
    std::vector<std::thread> workers_;
    int threadCount_ = 1;
    std::atomic<bool> stopping_{false};

    const RangeFn *body_ = nullptr;
    int64_t count_       = 0;
    int64_t chunkCount_  = 0;
    int64_t nextChunk_   = 0;
    int64_t doneChunks_  = 0;
    std::atomic<int64_t> doneCount_{0};   // mirror of doneChunks_ for lock-free polling
    std::atomic<uint64_t> generation_{0}; // bumped once per published region
    std::exception_ptr firstError_;
};

WorkerPool &pool() {
    static WorkerPool instance;
    return instance;
}

} // namespace

void parallelFor(int64_t count, int64_t grain, const RangeFn &fn) {
    if (count <= 0) {
        return;
    }
    pool().run(count, grain, fn);
}

int numThreads() { return pool().threads(); }

void setNumThreads(int threads) { pool().resize(threads); }

} // namespace camel::tensor::kernels
