// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Data-movement kernels: reshape, permute/transpose, concat, slice, and
 * element creation (arange, eye, random).
 *
 * Tensors are always contiguous, so every layout change materializes a new
 * buffer. Shape arithmetic helpers are exposed separately so static type
 * inference and the ONNX exporter compute exactly the same result shapes.
 */

#pragma once

#include "../tensor.h"

#include <optional>

namespace camel::tensor::kernels {

/**
 * Resolves a reshape target: at most one extent may be -1 (inferred), and the
 * element count must be preserved. Throws std::invalid_argument otherwise.
 */
Shape resolveReshape(std::span<const int64_t> input, std::span<const int64_t> target);

Array *
reshape(const Array *input, std::span<const int64_t> target, mm::IAllocator &allocator);

/// Shape after permuting axes by `perm` (validated to be a permutation).
Shape permuteShape(std::span<const int64_t> input, std::span<const int64_t> perm);
Array *
permute(const Array *input, std::span<const int64_t> perm, mm::IAllocator &allocator);

/// Swaps the last two axes (rank >= 2).
Array *transposeLast2(const Array *input, mm::IAllocator &allocator);

/// Shape after concatenating along `axis`. All operands must share rank and the other extents.
Shape concatShape(std::span<const std::span<const int64_t>> inputs, int64_t axis);
Array *
concat(std::span<const Array *const> inputs, int64_t axis, mm::IAllocator &allocator);

/// The bounds of a slice along an axis of `extent` elements, normalized: a negative bound
/// counts from the end of the axis. A bound still outside [0, extent] after that is out of
/// range: nullopt, and the operator faults (design principles 3.9: a `slice` out of bounds at
/// run time is a fault, as an index out of bounds is). An end before the start is the empty
/// range, not a fault (3.9).
struct SliceRange {
    int64_t start;
    int64_t end;
};
/// One bound of a slice along an axis of `extent` elements, normalized: a negative bound counts
/// from the end. nullopt when the bound is outside [-extent, extent] (the operator faults with
/// SliceOutOfRange). The single source of the bound rule: normalizeSliceBounds and the index
/// semantics declaration (ops/indexing.cpp) both go through it.
std::optional<int64_t> normalizeSliceBound(int64_t extent, int64_t bound);
std::optional<SliceRange> normalizeSliceBounds(int64_t extent, int64_t start, int64_t end);

/// Slice [start, end) with `step` along one axis. A bound out of range throws
/// std::out_of_range (normalizeSliceBounds); the operator checks first and faults with
/// SliceOutOfRange.
Array *slice(
    const Array *input, int64_t axis, int64_t start, int64_t end, int64_t step,
    mm::IAllocator &allocator);

/// Rows of `input` along its first axis, in the order `indices` gives: rank-1 `indices` select
/// rows, and the result is [indices.size(), input.shape[1:]]. The rows are copied: an arbitrary
/// row order is not a view of the input (design principles 5.4, 10.5).
Array *
take(const Array *input, std::span<const int64_t> indices, mm::IAllocator &allocator);

/// The gradient of take: `dy` scattered back into the shape of `input`, summing where an index
/// repeats. `indices` are the same ones `take` received.
Array *takeGrad(
    const Array *input, std::span<const int64_t> indices, const Array *dy,
    mm::IAllocator &allocator);

/// One-hot rows: a float32 tensor of shape [labels.size(), classes] with element
/// [i, labels[i]] = 1 and every other element 0.
Array *
oneHot(std::span<const int64_t> labels, int64_t classes, mm::IAllocator &allocator);

Array *arange(int64_t start, int64_t stop, int64_t step, mm::IAllocator &allocator);
Array *eye(int64_t size, mm::IAllocator &allocator);

/// Uniform [low, high) float32 samples from the module RNG.
Array *
randomUniform(std::span<const int64_t> shape, double low, double high, mm::IAllocator &allocator);
/// Normal(mean, std) float32 samples from the module RNG.
Array *
randomNormal(std::span<const int64_t> shape, double mean, double stddev, mm::IAllocator &allocator);
/// Reseeds the module RNG. Until called, the RNG is seeded from std::random_device. The samples
/// are drawn with camel/utils/random.h: after a seed they are the same on every platform.
void seedRandom(uint64_t seed);

} // namespace camel::tensor::kernels
