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
 * The only threading primitive tensor kernels use.
 *
 * `parallelFor` splits an index range into contiguous chunks and runs them on
 * a process-wide worker pool; the calling thread executes one chunk itself.
 * Only one parallel region runs at a time. If the pool is already busy (for
 * example because a parallel scheduler runs two tensor operators at once), the
 * second caller runs its range serially instead of queueing, which keeps
 * nested or concurrent use deadlock-free and avoids oversubscription.
 *
 * Chunk bodies run on worker threads, so they must not allocate GC objects or
 * otherwise touch the runtime; kernels allocate outputs before the region.
 *
 * The worker count defaults to the hardware concurrency and can be overridden
 * with the CAMEL_NUM_THREADS environment variable or `setNumThreads`.
 */

#pragma once

#include <cstdint>
#include <functional>

namespace camel::tensor::kernels {

/// Chunk body: processes indices [begin, end).
using RangeFn = std::function<void(int64_t begin, int64_t end)>;

/**
 * Runs `fn` over [0, count) in parallel when `count` is at least `2 * grain`.
 * `grain` is the smallest chunk worth handing to another thread.
 */
void parallelFor(int64_t count, int64_t grain, const RangeFn &fn);

/// Number of threads used by parallel regions (including the caller).
int numThreads();

/// Changes the thread count. Values < 1 are clamped to 1. Not thread-safe with running regions.
void setNumThreads(int threads);

} // namespace camel::tensor::kernels
