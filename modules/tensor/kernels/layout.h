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

/// One row's gradient added back at the index it was taken from: `acc` with `row` added where
/// `index` puts it, in the tensor's own shape and element type. This is `takeGrad` for a single
/// index and an accumulator that is already a gradient, which is what a loop's rounds need: a
/// round of `for row in t` takes the row `index` of the first dimension, so its gradient goes back
/// to that row (design principles 5.4).
Array *rowAddAt(
    const Array *acc, int64_t index, const Array *row, mm::IAllocator &allocator);

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
