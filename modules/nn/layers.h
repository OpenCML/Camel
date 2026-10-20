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
 * Tensor computations owned by the nn module (embedding gather and the fused
 * softmax cross-entropy loss) and the registration of nn's tensor operators
 * in the shared operator registry (see tensor_ops.cpp).
 */

#pragma once

#include "../tensor/tensor.h"

#include <span>

namespace camel::nn {

/// Gathers rows of a rank-2 table [rows, dim] by rank-1 int64 indices.
tensor::Array *embedding(
    const tensor::Array *table, const tensor::Array *indices,
    tensor::mm::IAllocator &allocator);

/// Dense table gradient for `embedding` (unvisited rows stay zero).
tensor::Array *embeddingTableGrad(
    const tensor::Array *table, const tensor::Array *indices,
    const tensor::Array *dy, tensor::mm::IAllocator &allocator);

/// Mean softmax cross-entropy of rank-2 logits against a same-shaped target distribution.
double softmaxCrossEntropy(const tensor::Array *logits, const tensor::Array *target);

/// Gradient of `softmaxCrossEntropy` w.r.t. the logits, scaled by `dy`.
tensor::Array *softmaxCrossEntropyGradLogits(
    const tensor::Array *logits, const tensor::Array *target, double dy,
    tensor::mm::IAllocator &allocator);

/// Fused softmax cross-entropy of rank-2 logits [batch, classes] against integer class labels
/// (one per row): the value is the same as `softmaxCrossEntropy` with the labels one-hot.
double softmaxCrossEntropyLabels(
    const tensor::Array *logits, std::span<const int64_t> labels);

/// Gradient of `softmaxCrossEntropyLabels` w.r.t. the logits, scaled by `dy`.
tensor::Array *softmaxCrossEntropyLabelsGradLogits(
    const tensor::Array *logits, std::span<const int64_t> labels, double dy,
    tensor::mm::IAllocator &allocator);

/// Registers nn's tensor operators under the "nn" protocol (idempotent).
void registerNnTensorOps();

} // namespace camel::nn
