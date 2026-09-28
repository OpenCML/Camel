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

namespace camel::nn {

/// Gathers rows of a rank-2 table [rows, dim] by rank-1 int64 indices.
tensor::TensorObject *embedding(
    const tensor::TensorObject *table, const tensor::TensorObject *indices,
    tensor::mm::IAllocator &allocator);

/// Dense table gradient for `embedding` (unvisited rows stay zero).
tensor::TensorObject *embeddingTableGrad(
    const tensor::TensorObject *table, const tensor::TensorObject *indices,
    const tensor::TensorObject *dy, tensor::mm::IAllocator &allocator);

/// Mean softmax cross-entropy of rank-2 logits against a same-shaped target distribution.
double softmaxCrossEntropy(const tensor::TensorObject *logits, const tensor::TensorObject *target);

/// Gradient of `softmaxCrossEntropy` w.r.t. the logits, scaled by `dy`.
tensor::TensorObject *softmaxCrossEntropyGradLogits(
    const tensor::TensorObject *logits, const tensor::TensorObject *target, double dy,
    tensor::mm::IAllocator &allocator);

/// Registers nn's tensor operators under the "nn" protocol (idempotent).
void registerNnTensorOps();

} // namespace camel::nn
