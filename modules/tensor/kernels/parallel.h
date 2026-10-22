// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The only threading primitive tensor kernels use.
 *
 * `parallelFor` splits an index range into contiguous chunks and runs them on
 * a process-wide worker pool; the calling thread executes one chunk itself.
 * Only one parallel region runs at a time. If the pool is already busy (for
 * example because a parallel scheduler runs two tensor operators at once), the
 * second caller runs its range serially instead of queueing, which keeps
 * nested or concurrent use deadlock-free and avoids oversubscription.
 *
 * A range that is too small to pay for a worker is not a parallel region at
 * all: it runs on the calling thread, and the pool is not created for it (see
 * `kMinWorkPerWorkerNs`).
 *
 * Chunk bodies run on worker threads, so they must not allocate GC objects or
 * otherwise touch the runtime; kernels allocate outputs before the region.
 *
 * The worker count defaults to the hardware concurrency. A run's `-t` sets it for every pool,
 * and the tensor module installs that number once when it loads (setNumThreads); a program can
 * change it later with `setNumThreads` / `Tensor.set_num_threads`. No environment variable takes
 * part: a setting is not an environment variable (design principles 9.3).
 */

#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace camel::tensor::kernels {

/// Chunk body: processes indices [begin, end).
using RangeFn = std::function<void(int64_t begin, int64_t end)>;

/**
 * The least serial work, in nanoseconds, that pays for handing a chunk to
 * another thread.
 *
 * Handing out a chunk costs a wakeup, a claim under the pool's state lock and a
 * join; on the reference host (32-core EPYC 7343, Release, load average 25-54
 * from the other work on the machine) a region's remaining chunks were still
 * running tens of microseconds after the caller had finished its own. The
 * break-even was measured on this host by running one operator per round with
 * one thread (all inline) against the pool, medians of 300 rounds:
 *
 *   f32 broadcast multiply, 262144 elements   46 us inline   33 us over 8 chunks
 *   f32 broadcast multiply, 1048576 elements 181 us inline  144 us over 8 chunks
 *   matmul 128^3                              45 us inline   30 us over 2 chunks
 *   matmul 256^3                             334 us inline  156 us over 8 chunks
 *
 * and against it, on the loaded host, a 32x784x128 product (three chunks of
 * ~25 us) was 49 us inline against 82 us over three threads: below about 40 us
 * of work per chunk a split was a wash or a loss, above it a clear win. The
 * policy therefore asks for 40 us of work per worker; a range whose chunks
 * would be smaller runs inline, and the pool is not created for it.
 *
 * `kNsPerFloat` is the serial cost of one element of a plain float pass, the
 * unit the elementwise kernels count in (1 M elements of a broadcast multiply:
 * 181 us, bandwidth-bound, 0.17 ns each). Kernels convert their own unit cost
 * with `grainForWork`, so the policy lives here and nowhere else.
 */
constexpr int64_t kMinWorkPerWorkerNs = 40000;

/// Serial cost of one element of a plain float pass over a contiguous buffer.
constexpr double kNsPerFloat = 0.17;

/**
 * Units per chunk that make one chunk worth a thread, given the serial cost of
 * one unit in nanoseconds (an estimate from measurement, not a measurement per
 * call). Returns 1 for a non-positive cost, so a caller can never divide by 0.
 */
constexpr inline int64_t grainForWork(double nsPerUnit) {
    if (!(nsPerUnit > 0.0)) {
        return 1;
    }
    const double units = static_cast<double>(kMinWorkPerWorkerNs) / nsPerUnit;
    if (units <= 1.0) {
        return 1;
    }
    return static_cast<int64_t>(units);
}

/**
 * True when `parallelFor` would spread [0, count) over more than one thread:
 * at least two chunks of `grain` units exist, the pool has more than one
 * thread, and the caller is not already inside a region. Creates nothing.
 */
bool parallelWouldSplit(int64_t count, int64_t grain) noexcept;

/// Publishes one region to the pool. Called only when `parallelWouldSplit` was true.
void runParallelChunks(int64_t count, int64_t grain, const RangeFn &fn);

/**
 * Runs `fn` over [0, count) in parallel when the range holds at least two
 * chunks of `grain` units (see `kMinWorkPerWorkerNs`); otherwise calls it once
 * on the calling thread, without touching the pool or its locks.
 */
template <typename Fn> inline void parallelFor(int64_t count, int64_t grain, Fn &&fn) {
    if (count <= 0) {
        return;
    }
    if (!parallelWouldSplit(count, grain)) {
        fn(static_cast<int64_t>(0), count);
        return;
    }
    runParallelChunks(count, grain, RangeFn(std::forward<Fn>(fn)));
}

/// Number of threads a parallel region may use (including the caller).
int numThreads();

/// Changes the thread count. Values < 1 are clamped to 1. Not thread-safe with running regions.
void setNumThreads(int threads);

} // namespace camel::tensor::kernels