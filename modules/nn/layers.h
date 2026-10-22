// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
