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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * DLPack import and export without a copy. See the header for the ownership rules.
 */

#include "camel/core/array/dlpack.h"

#include "camel/core/mm.h"
#include "camel/core/mm/block.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/assert.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace camel::core::array {

using camel::core::mm::Block;
using camel::core::mm::BlockRelease;
using camel::core::type::TypeCode;

namespace {

DLDataType toDLPackType(TypeCode code) {
    switch (code) {
    case TypeCode::Float32:
        return DLDataType{2, 32, 1};
    case TypeCode::Float64:
        return DLDataType{2, 64, 1};
    case TypeCode::Int32:
        return DLDataType{0, 32, 1};
    case TypeCode::Int64:
        return DLDataType{0, 64, 1};
    case TypeCode::Bool:
        return DLDataType{6, 8, 1};
    case TypeCode::Byte:
        return DLDataType{1, 8, 1};
    default:
        throw std::invalid_argument("Unsupported dtype for DLPack export");
    }
}

TypeCode fromDLPackType(const DLDataType &dtype) {
    if (dtype.lanes != 1) {
        throw std::invalid_argument("Only lanes=1 DLPack tensors are supported");
    }
    if (dtype.code == 2 && dtype.bits == 32) {
        return TypeCode::Float32;
    }
    if (dtype.code == 2 && dtype.bits == 64) {
        return TypeCode::Float64;
    }
    if (dtype.code == 0 && dtype.bits == 32) {
        return TypeCode::Int32;
    }
    if (dtype.code == 0 && dtype.bits == 64) {
        return TypeCode::Int64;
    }
    if (dtype.code == 6 && dtype.bits == 8) {
        return TypeCode::Bool;
    }
    if (dtype.code == 1 && dtype.bits == 8) {
        return TypeCode::Byte;
    }
    throw std::invalid_argument("Unsupported DLPack dtype");
}

uint64_t numelOf(std::span<const int64_t> extents) {
    uint64_t total = 1;
    for (int64_t extent : extents) {
        if (extent < 0) {
            throw std::invalid_argument("DLPack shape cannot contain negative extents");
        }
        total *= static_cast<uint64_t>(extent);
    }
    return total;
}

struct ExportContext {
    Block *block   = nullptr;
    int64_t *shape = nullptr;
};

void releaseExport(DLManagedTensor *self) {
    if (self == nullptr) {
        return;
    }
    auto *context = static_cast<ExportContext *>(self->manager_ctx);
    if (context != nullptr) {
        if (context->block != nullptr) {
            context->block->release();
        }
        delete[] context->shape;
        delete context;
    }
    delete self;
}

struct ImportContext {
    const DLManagedTensor *managed = nullptr;
};

void releaseImport(void *raw) noexcept {
    auto *context = static_cast<ImportContext *>(raw);
    if (context != nullptr) {
        if (context->managed != nullptr && context->managed->deleter != nullptr) {
            context->managed->deleter(const_cast<DLManagedTensor *>(context->managed));
        }
        delete context;
    }
}

} // namespace

DLManagedTensor *toDLPack(Array *tensor) {
    ASSERT(tensor != nullptr, "Cannot export a null array");
    ASSERT(tensor->isCompact(), "Only a compact pure-data array can be exported through DLPack");
    // DLPack has no dtype for a struct: an array of structs is left to the Arrow item.
    ASSERT(
        camel::core::type::isCompactElementType(tensor->dtype()),
        "Only an array of numeric scalars can be exported through DLPack");
    // A view or an export needs a stable data address; an inline array is promoted (once).
    tensor->ensureBlock();
    Block *block = tensor->block();
    ASSERT(block != nullptr, "A block-backed array has a block");

    auto *context  = new ExportContext();
    context->block = block;
    block->retain();
    const size_t rank = tensor->rank();
    context->shape    = rank > 0 ? new int64_t[rank] : nullptr;
    for (size_t i = 0; i < rank; ++i) {
        context->shape[i] = tensor->dim(i);
    }

    auto *managed              = new DLManagedTensor{};
    managed->manager_ctx       = context;
    managed->deleter           = &releaseExport;
    managed->dl_tensor.data    = tensor->rawData();
    managed->dl_tensor.device  = DLDevice{kDLCPU, 0};
    managed->dl_tensor.ndim    = static_cast<int32_t>(rank);
    managed->dl_tensor.dtype   = toDLPackType(tensor->dtype());
    managed->dl_tensor.shape   = context->shape;
    managed->dl_tensor.strides = nullptr;
    managed->dl_tensor.byte_offset = 0;
    return managed;
}

Array *fromDLPack(
    const DLManagedTensor *managed, camel::core::mm::IAllocator &allocator) {
    if (managed == nullptr) {
        throw std::invalid_argument("Null DLPack tensor");
    }
    if (managed->dl_tensor.device.device_type != kDLCPU) {
        throw std::invalid_argument("Only CPU DLPack tensors are supported");
    }
    if (managed->dl_tensor.strides != nullptr || managed->dl_tensor.byte_offset != 0) {
        throw std::invalid_argument("Only contiguous DLPack tensors are supported");
    }
    if (managed->dl_tensor.ndim < 0) {
        throw std::invalid_argument("DLPack tensor has a negative rank");
    }
    const TypeCode dtype = fromDLPackType(managed->dl_tensor.dtype);
    std::vector<int64_t> shape;
    if (managed->dl_tensor.ndim > 0) {
        if (managed->dl_tensor.shape == nullptr) {
            throw std::invalid_argument("DLPack tensor has no shape");
        }
        shape.assign(managed->dl_tensor.shape, managed->dl_tensor.shape + managed->dl_tensor.ndim);
    }
    const uint64_t elements = numelOf(shape);
    const size_t elementSize = type::arrayElementSize(dtype);
    const size_t bytes       = static_cast<size_t>(elements) * elementSize;

    const type::ArrayType *arrayType = type::ArrayType::create(type::Type::create(dtype));

    if (&allocator != &camel::core::mm::autoSpace()) {
        // This heap cannot run the array's finalizer, so sharing the producer's memory would
        // leak its reference. Copy instead, as the tensor module's import always did.
        Array *copy = Array::create(allocator, arrayType, shape, false);
        if (bytes > 0) {
            std::memcpy(copy->rawData(), managed->dl_tensor.data, bytes);
        }
        return copy;
    }

    if (bytes > 0 && managed->dl_tensor.data == nullptr) {
        throw std::invalid_argument("DLPack tensor has no data");
    }
    auto *context  = new ImportContext{managed};
    Block *block = Block::adoptExternal(
        managed->dl_tensor.data, bytes, BlockRelease{&releaseImport, context});
    Array *array = Array::wrap(allocator, arrayType, shape, block);
    // `wrap` retained it; the caller's reference is this one.
    block->release();
    return array;
}

} // namespace camel::core::array