// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The positions a range takes along one axis (design principles 5.4).
 *
 * A slice is a range read on one axis: the walk starts at the first position it takes, moves by
 * `step`, and stops at a bound it does not take. The ends are bounds of an integer sequence and
 * not positions, so a forward walk stops one past its last position and a backward walk one before
 * its first -- `range(3, -1, -1)` is 3, 2, 1, 0. An end the program did not write is the axis's own
 * on that side, which is what makes `t[..]` the whole axis and `t[.., 0]` a position of the second
 * dimension.
 *
 * This is the one place the arithmetic lives: the slice kernels at the run, the static check that
 * refuses a constant range outside its axis, the shape a slice of a stated extent gives, and the
 * ONNX lowering of the same node all read their positions here.
 */

#pragma once

#include <cstdint>
#include <optional>

namespace camel::core {

/// The positions a range takes along one axis, and whether every one of them is a position that
/// axis has. `outside` is read only when the extent is known: with no extent nothing is said about
/// the axis, and the run is the one that finds a position missing.
struct SlicePositions {
    int64_t start = 0;     // the first position the walk takes
    int64_t stop  = 0;     // the bound the walk stops at, which it never takes
    int64_t count = 0;     // how many positions the walk takes
    int64_t step  = 1;     // the distance between two positions taken
    bool outside  = false; // a position the walk takes is not one the axis has
};

/// The bound an end left out names: the axis's first position, or the one past its last, for a
/// forward walk; its last position, or the one before its first, for a backward one. Nothing when
/// the bound is the axis's own and the extent is not known. A bound is not a position: the two
/// past-the-ends bounds name places the axis does not have, and the walk stops there without
/// taking them.
inline std::optional<int64_t>
sliceUnwrittenBound(int64_t step, std::optional<int64_t> extent, bool isStart) {
    if (isStart) {
        if (step > 0) {
            return 0;
        }
        return extent ? std::optional<int64_t>(*extent - 1) : std::nullopt;
    }
    if (step > 0) {
        return extent ? std::optional<int64_t>(*extent) : std::nullopt;
    }
    return -1;
}

/// The positions the range from `start` up to `stop` takes along an axis of `extent` positions. An
/// end that is not given is `sliceUnwrittenBound`'s. Nothing when the question has no answer: a
/// step of 0 walks nowhere, and an end left out on an axis whose extent is not known stands for
/// nothing yet.
inline std::optional<SlicePositions> slicePositions(
    std::optional<int64_t> extent, std::optional<int64_t> start, std::optional<int64_t> stop,
    int64_t step) {
    if (step == 0) {
        return std::nullopt;
    }
    SlicePositions positions;
    positions.step = step;
    if (!start) {
        start = sliceUnwrittenBound(step, extent, /*isStart=*/true);
    }
    if (!stop) {
        stop = sliceUnwrittenBound(step, extent, /*isStart=*/false);
    }
    if (!start || !stop) {
        return std::nullopt;
    }
    positions.start = *start;
    positions.stop  = *stop;
    positions.count = step > 0 ? (positions.start < positions.stop
                                      ? (positions.stop - positions.start + step - 1) / step
                                      : 0)
                               : (positions.start > positions.stop
                                      ? (positions.start - positions.stop - step - 1) / (-step)
                                      : 0);
    if (extent) {
        bool outside = positions.start > *extent || positions.stop > *extent;
        if (positions.count > 0) {
            const int64_t last = positions.start + (positions.count - 1) * step;
            outside            = outside || positions.start < 0 || last < 0 || last >= *extent;
        }
        positions.outside = outside;
    }
    return positions;
}

} // namespace camel::core