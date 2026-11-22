// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Allocation accounting for `std::profile` (camel/profile/profile.h).
 *
 * A kernel's wall time mixes the arithmetic, the packing it does to feed it, and the allocation
 * of its result; a timer on the operator boundary cannot tell them apart. This header makes the
 * allocator part separable: the Camel memory spaces time the allocations they serve, and the
 * session attributes each one to the operator call on the same thread. The report then says, for
 * every operator row, how much of its wall time was inside the allocators, how many allocations
 * it made, how many of them were data blocks, and how many payload bytes they asked for.
 *
 * What is counted is exactly what the Camel allocators serve:
 *
 *   - a data block's payload from the block space (`mm::Block::create` / `grow`), counted as a
 *     block with the payload length the caller asked for;
 *   - an object from the collected heap (`mm::autoSpace()`), counted as an allocation;
 *   - an object from the permanent space (`mm::permSpace()`), counted as an allocation.
 *
 * The graph and meta spaces are compile-time spaces and are not counted: an operator call does
 * not allocate the program. A `malloc` behind `new`, a `std::vector`'s buffer and a copy the
 * kernel makes with `memcpy` are not allocator calls either and stay invisible (see the report
 * and docs/cli.md, "Profiling a run", for the limits this leaves).
 *
 * Off by default, like the session: with no `std::profile` in the pipeline each scope is one
 * relaxed load and a not-taken branch. On, one scope costs a timestamp pair and a few relaxed
 * atomic adds; the report prints what a timestamp pair costs on the machine that ran it.
 */

#pragma once

#include "camel/profile/profile.h"

#include <chrono>
#include <cstdint>

namespace camel::profile {

/// One allocation the Camel allocators served: `ns` inside the allocator, `bytes` requested and
/// whether it was a data block's payload. Attributed to the operator call on this thread when
/// there is one, and counted apart otherwise. Does nothing while the session is off.
void noteAllocation(uint64_t ns, uint64_t bytes, bool block) noexcept;

/// Times one allocation call while the session records. `bytes` is what the caller asked the
/// allocator for; for a block it is the payload length (`Block::create`'s argument), not the
/// block space's rounded-up request.
class AllocationScope final {
  public:
    AllocationScope(uint64_t bytes, bool block) noexcept : bytes_(bytes), block_(block) {
        if (enabledInline()) {
            start_   = std::chrono::steady_clock::now();
            running_ = true;
        }
    }

    ~AllocationScope() noexcept {
        if (!running_) {
            return;
        }
        const uint64_t ns = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start_)
                .count());
        noteAllocation(ns, bytes_, block_);
    }

    AllocationScope(const AllocationScope &)            = delete;
    AllocationScope &operator=(const AllocationScope &) = delete;

  private:
    uint64_t bytes_ = 0;
    bool block_     = false;
    bool running_   = false;
    std::chrono::steady_clock::time_point start_{};
};

} // namespace camel::profile