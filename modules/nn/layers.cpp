// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Fused neural-network layer primitives.
 *
 * `embedding` is a small true gather over a rank-2 parameter table. Its VJP uses
 * a dense table-gradient tensor so existing Parameter/SGD machinery can update
 * only visited rows while leaving unvisited rows with zero gradients.
 */

#include "layers.h"

#include "../tensor/tensor.h"
#include "../tensor/type.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"

#include <stdexcept>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace camel::nn {

namespace tensor = camel::tensor;

namespace {

void requireEmbeddingInputs(
    const tensor::Array *table, const tensor::Array *indices, const char *what) {
    if (!table || table->rank() != 2) {
        throw std::invalid_argument(std::string(what) + " expects a rank-2 table [rows, dim]");
    }
    if (!tensor::isFloatingTensorType(table->dtype())) {
        throw std::invalid_argument(std::string(what) + " table must be floating point");
    }
    if (!indices || indices->rank() != 1) {
        throw std::invalid_argument(std::string(what) + " expects rank-1 integer indices");
    }
    if (indices->dtype() != TypeCode::Int64) {
        throw std::invalid_argument(std::string(what) + " indices must be an int64 Tensor");
    }
}

int64_t checkedIndex(const tensor::Array *indices, uint64_t offset, int64_t rows) {
    const int64_t index = indices->getAsInt64(offset);
    if (index < 0 || index >= rows) {
        throw std::invalid_argument("embedding index out of range");
    }
    return index;
}

} // namespace

tensor::Array *embedding(
    const tensor::Array *table, const tensor::Array *indices,
    mm::IAllocator &allocator) {
    constexpr const char *kWhat = "embedding";
    requireEmbeddingInputs(table, indices, kWhat);

    const int64_t count = indices->dim(0);
    const int64_t dim   = table->dim(1);
    int64_t shape[]     = {count, dim};
    tensor::Array *out =
        tensor::createTensor(table->dtype(), shape, allocator, false);

    for (int64_t row = 0; row < count; ++row) {
        const int64_t sourceRow = checkedIndex(indices, static_cast<uint64_t>(row), table->dim(0));
        for (int64_t col = 0; col < dim; ++col) {
            const uint64_t src = static_cast<uint64_t>(sourceRow * dim + col);
            const uint64_t dst = static_cast<uint64_t>(row * dim + col);
            out->setFromDouble(dst, table->getAsDouble(src));
        }
    }

    return out;
}

tensor::Array *embeddingTableGrad(
    const tensor::Array *table, const tensor::Array *indices,
    const tensor::Array *dy, mm::IAllocator &allocator) {
    constexpr const char *kWhat = "embedding_table_grad";
    requireEmbeddingInputs(table, indices, kWhat);
    if (!dy || dy->rank() != 2 || dy->dim(0) != indices->dim(0) || dy->dim(1) != table->dim(1)) {
        throw std::invalid_argument(
            "embedding_table_grad requires dy shape [len(indices), table_dim]");
    }
    if (!tensor::isFloatingTensorType(dy->dtype())) {
        throw std::invalid_argument("embedding_table_grad dy must be floating point");
    }

    tensor::Array *grad = tensor::createTensor(
        table->dtype(),
        std::span<const int64_t>(table->shape(), table->rank()),
        allocator,
        true);
    const int64_t count = indices->dim(0);
    const int64_t dim   = table->dim(1);
    for (int64_t row = 0; row < count; ++row) {
        const int64_t targetRow = checkedIndex(indices, static_cast<uint64_t>(row), table->dim(0));
        for (int64_t col = 0; col < dim; ++col) {
            const uint64_t src = static_cast<uint64_t>(row * dim + col);
            const uint64_t dst = static_cast<uint64_t>(targetRow * dim + col);
            grad->setFromDouble(dst, grad->getAsDouble(dst) + dy->getAsDouble(src));
        }
    }

    return grad;
}

} // namespace camel::nn
