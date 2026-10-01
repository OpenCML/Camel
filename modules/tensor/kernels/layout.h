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

TensorObject *
reshape(const TensorObject *input, std::span<const int64_t> target, mm::IAllocator &allocator);

/// Shape after permuting axes by `perm` (validated to be a permutation).
Shape permuteShape(std::span<const int64_t> input, std::span<const int64_t> perm);
TensorObject *
permute(const TensorObject *input, std::span<const int64_t> perm, mm::IAllocator &allocator);

/// Swaps the last two axes (rank >= 2).
TensorObject *transposeLast2(const TensorObject *input, mm::IAllocator &allocator);

/// Shape after concatenating along `axis`. All operands must share rank and the other extents.
Shape concatShape(std::span<const std::span<const int64_t>> inputs, int64_t axis);
TensorObject *
concat(std::span<const TensorObject *const> inputs, int64_t axis, mm::IAllocator &allocator);

/// Slice [start, end) with `step` along one axis. Bounds clamp like Python slices.
TensorObject *slice(
    const TensorObject *input, int64_t axis, int64_t start, int64_t end, int64_t step,
    mm::IAllocator &allocator);

TensorObject *arange(int64_t start, int64_t stop, int64_t step, mm::IAllocator &allocator);
TensorObject *eye(int64_t size, mm::IAllocator &allocator);

/// Uniform [low, high) float32 samples from the module RNG.
TensorObject *
randomUniform(std::span<const int64_t> shape, double low, double high, mm::IAllocator &allocator);
/// Normal(mean, std) float32 samples from the module RNG.
TensorObject *
randomNormal(std::span<const int64_t> shape, double mean, double stddev, mm::IAllocator &allocator);
/// Reseeds the module RNG. Until called, the RNG is seeded from std::random_device.
void seedRandom(uint64_t seed);

} // namespace camel::tensor::kernels
