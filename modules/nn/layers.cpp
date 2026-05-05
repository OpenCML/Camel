/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 05, 2026
 * Updated: May. 05, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Fused neural-network layer primitives.
 *
 * `embedding` is a small true gather over a rank-2 parameter table. Its VJP uses
 * a dense table-gradient tensor so existing Parameter/SGD machinery can update
 * only visited rows while leaving unvisited rows with zero gradients.
 */

#include "operators.h"

#include "../tensor/runtime.h"
#include "../tensor/type.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"

#include <stdexcept>

namespace mm = camel::core::mm;
using namespace camel::core::context;
using namespace camel::core::error;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

namespace tensor = camel::tensor;

bool isTensorType(Type *type) { return type && type->code() == tensor::TensorType::typeCode(); }

tensor::TensorObject *requireTensor(ArgsView &norm, size_t index, const char *what) {
    if (!isTensorType(norm.type(index))) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "{} expects Tensor arguments", what);
    }
    auto *value = norm.get<tensor::TensorObject *>(index);
    if (!value) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, "{} received a null Tensor", what);
    }
    return value;
}

void requireEmbeddingInputs(
    const tensor::TensorObject *table, const tensor::TensorObject *indices, const char *what) {
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

int64_t checkedIndex(const tensor::TensorObject *indices, uint64_t offset, int64_t rows) {
    const int64_t index = indices->getAsInt64(offset);
    if (index < 0 || index >= rows) {
        throw std::invalid_argument("embedding index out of range");
    }
    return index;
}

tensor::TensorObject *embedding(
    const tensor::TensorObject *table, const tensor::TensorObject *indices,
    mm::IAllocator &allocator) {
    constexpr const char *kWhat = "embedding";
    requireEmbeddingInputs(table, indices, kWhat);

    const int64_t count = indices->dim(0);
    const int64_t dim   = table->dim(1);
    int64_t shape[]     = {count, dim};
    tensor::TensorObject *out =
        tensor::TensorObject::create(table->dtype(), shape, allocator, false);

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

tensor::TensorObject *embeddingTableGrad(
    const tensor::TensorObject *table, const tensor::TensorObject *indices,
    const tensor::TensorObject *dy, mm::IAllocator &allocator) {
    constexpr const char *kWhat = "embedding_table_grad";
    requireEmbeddingInputs(table, indices, kWhat);
    if (!dy || dy->rank() != 2 || dy->dim(0) != indices->dim(0) || dy->dim(1) != table->dim(1)) {
        throw std::invalid_argument(
            "embedding_table_grad requires dy shape [len(indices), table_dim]");
    }
    if (!tensor::isFloatingTensorType(dy->dtype())) {
        throw std::invalid_argument("embedding_table_grad dy must be floating point");
    }

    tensor::TensorObject *grad = tensor::tensorZeros(
        table->dtype(),
        std::span<const int64_t>(table->shape(), table->rank()),
        allocator);
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

template <typename Fn> slot_t withLayerErrors(Fn &&fn) {
    try {
        return fn();
    } catch (const std::exception &e) {
        throwRuntimeFault(RuntimeDiag::RuntimeError, e.what());
    }
}

} // namespace

slot_t __nn_embedding__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return withLayerErrors([&]() -> slot_t {
        tensor::TensorObject *out = embedding(
            requireTensor(norm, 0, "embedding"),
            requireTensor(norm, 1, "embedding"),
            mm::autoSpace());
        return toSlot(static_cast<Object *>(out));
    });
}

slot_t __nn_embedding_table_grad__(ArgsView &with, ArgsView &norm, Context &ctx) {
    (void)with;
    (void)ctx;
    return withLayerErrors([&]() -> slot_t {
        tensor::TensorObject *grad = embeddingTableGrad(
            requireTensor(norm, 0, "embedding_table_grad"),
            requireTensor(norm, 1, "embedding_table_grad"),
            requireTensor(norm, 2, "embedding_table_grad"),
            mm::autoSpace());
        return toSlot(static_cast<Object *>(grad));
    });
}
