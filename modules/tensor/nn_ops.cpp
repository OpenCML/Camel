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
 * Author: Zhenjie Wei
 * Created: May. 05, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Small neural-network tensor primitives.
 *
 * These functions keep Phase 2 attention support narrow and explicit: row-wise
 * rank-2 softmax only, with a matching fused VJP primitive for reverse mode.
 */

#include "runtime.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace camel::tensor {

namespace {

void requireRank2Float(const TensorObject *tensor, const char *what) {
    if (!tensor || tensor->rank() != 2) {
        throw std::invalid_argument(std::string(what) + " currently requires a rank-2 tensor");
    }
    if (!isFloatingTensorType(tensor->dtype())) {
        throw std::invalid_argument(std::string(what) + " requires a floating-point tensor");
    }
    if (tensor->dim(0) <= 0 || tensor->dim(1) <= 0) {
        throw std::invalid_argument(std::string(what) + " requires non-empty dimensions");
    }
}

} // namespace

TensorObject *tensorSoftmaxRows2D(const TensorObject *tensor, mm::IAllocator &allocator) {
    requireRank2Float(tensor, "softmax");
    TensorObject *out = TensorObject::create(
        type::TypeCode::Float32,
        std::span<const int64_t>(tensor->shape(), tensor->rank()),
        allocator,
        false);

    const int64_t rows = tensor->dim(0);
    const int64_t cols = tensor->dim(1);
    for (int64_t row = 0; row < rows; ++row) {
        const uint64_t rowOffset = static_cast<uint64_t>(row * cols);
        double rowMax            = tensor->getAsDouble(rowOffset);
        for (int64_t col = 1; col < cols; ++col) {
            rowMax = std::max(rowMax, tensor->getAsDouble(rowOffset + static_cast<uint64_t>(col)));
        }

        double expSum = 0.0;
        for (int64_t col = 0; col < cols; ++col) {
            const double value =
                std::exp(tensor->getAsDouble(rowOffset + static_cast<uint64_t>(col)) - rowMax);
            out->setFromDouble(rowOffset + static_cast<uint64_t>(col), value);
            expSum += value;
        }
        for (int64_t col = 0; col < cols; ++col) {
            const uint64_t index = rowOffset + static_cast<uint64_t>(col);
            out->setFromDouble(index, out->getAsDouble(index) / expSum);
        }
    }

    return out;
}

TensorObject *tensorSoftmaxRows2DGrad(
    const TensorObject *output, const TensorObject *dy, mm::IAllocator &allocator) {
    requireRank2Float(output, "softmax_grad");
    requireRank2Float(dy, "softmax_grad");
    if (!output->sameShape(dy)) {
        throw std::invalid_argument("softmax_grad requires output and dy shapes to match");
    }

    TensorObject *grad = TensorObject::create(
        type::TypeCode::Float32,
        std::span<const int64_t>(output->shape(), output->rank()),
        allocator,
        false);

    const int64_t rows = output->dim(0);
    const int64_t cols = output->dim(1);
    for (int64_t row = 0; row < rows; ++row) {
        const uint64_t rowOffset = static_cast<uint64_t>(row * cols);
        double dot               = 0.0;
        for (int64_t col = 0; col < cols; ++col) {
            const uint64_t index = rowOffset + static_cast<uint64_t>(col);
            dot += dy->getAsDouble(index) * output->getAsDouble(index);
        }
        for (int64_t col = 0; col < cols; ++col) {
            const uint64_t index = rowOffset + static_cast<uint64_t>(col);
            const double y       = output->getAsDouble(index);
            grad->setFromDouble(index, y * (dy->getAsDouble(index) - dot));
        }
    }

    return grad;
}

} // namespace camel::tensor
