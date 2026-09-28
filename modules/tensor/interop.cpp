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
 * Created: Mar. 10, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tensor interop conversions (see interop.h).
 */

#include "interop.h"

#include "camel/core/type/composite/array.h"

#include <cstring>
#include <stdexcept>

namespace camel::tensor {

using namespace camel::core::rtdata;
using type::TypeCode;

namespace {

struct ArrayInference {
    type::TypeCode dtype;
    std::vector<int64_t> shape;
};

ArrayInference inferArrayType(slot_t slot, type::Type *valueType) {
    if (valueType->code() == type::TypeCode::Array) {
        auto *arrayType      = static_cast<type::ArrayType *>(valueType);
        auto *array          = rtdata::fromSlot<Array *>(slot);
        int64_t size         = array ? static_cast<int64_t>(array->size()) : 0;
        type::Type *elemType = arrayType->elemType();
        type::TypeCode inferredDType;
        if (elemType->code() == type::TypeCode::Array) {
            inferredDType = type::TypeCode::Float32;
        } else {
            inferredDType = normalizeTensorDType(arrayType->elemTypeCode());
        }

        ArrayInference inferred{inferredDType, {size}};
        if (!array || size == 0) {
            return inferred;
        }

        if (elemType->code() == type::TypeCode::Array) {
            ArrayInference child = inferArrayType(array->data()[0], elemType);
            inferred.dtype       = child.dtype;
            inferred.shape.insert(inferred.shape.end(), child.shape.begin(), child.shape.end());
            for (size_t i = 1; i < array->size(); ++i) {
                ArrayInference another = inferArrayType(array->data()[i], elemType);
                if (another.shape != child.shape || another.dtype != child.dtype) {
                    throw std::invalid_argument("Nested array shape or dtype mismatch");
                }
            }
        } else {
            inferred.dtype = normalizeTensorDType(elemType->code());
        }
        return inferred;
    }

    if (!isSupportedTensorScalar(valueType->code())) {
        throw std::invalid_argument("Only numeric arrays can be converted to Tensor");
    }
    return ArrayInference{normalizeTensorDType(valueType->code()), {}};
}

void flattenArray(
    const Array *array, type::Type *valueType, TensorObject *tensor, uint64_t &offset) {
    auto *arrayType      = static_cast<type::ArrayType *>(valueType);
    type::Type *elemType = arrayType->elemType();
    for (size_t i = 0; i < array->size(); ++i) {
        slot_t value = array->data()[i];
        if (elemType->code() == type::TypeCode::Array) {
            auto *child = rtdata::fromSlot<Array *>(value);
            flattenArray(child, elemType, tensor, offset);
            continue;
        }

        switch (tensor->dtype()) {
        case type::TypeCode::Float32:
            tensor->setFromDouble(offset++, scalarToDouble(elemType->code(), value));
            break;
        case type::TypeCode::Int64:
            tensor->setFromInt64(offset++, scalarToInt64(elemType->code(), value));
            break;
        case type::TypeCode::Bool:
            tensor->setFromBool(offset++, scalarToBool(elemType->code(), value));
            break;
        default:
            throw std::invalid_argument("Unsupported Tensor dtype");
        }
    }
}

DLDataType toDLPackType(type::TypeCode dtype) {
    switch (dtype) {
    case type::TypeCode::Float32:
        return DLDataType{2, 32, 1};
    case type::TypeCode::Int64:
        return DLDataType{0, 64, 1};
    case type::TypeCode::Bool:
        return DLDataType{6, 8, 1};
    default:
        throw std::invalid_argument("Unsupported dtype for DLPack export");
    }
}

type::TypeCode fromDLPackType(const DLDataType &dtype) {
    if (dtype.lanes != 1) {
        throw std::invalid_argument("Only lanes=1 DLPack tensors are supported");
    }
    if (dtype.code == 2 && dtype.bits == 32) {
        return type::TypeCode::Float32;
    }
    if (dtype.code == 0 && dtype.bits == 64) {
        return type::TypeCode::Int64;
    }
    if (dtype.code == 6 && dtype.bits == 8) {
        return type::TypeCode::Bool;
    }
    throw std::invalid_argument("Unsupported DLPack dtype");
}
struct DLPackCopyContext {
    int64_t *shape;
    void *data;
};

} // namespace

double scalarToDouble(type::TypeCode code, slot_t value) {
    switch (code) {
    case type::TypeCode::Int32:
        return static_cast<double>(rtdata::fromSlot<rtdata::Int32>(value));
    case type::TypeCode::Int64:
        return static_cast<double>(rtdata::fromSlot<rtdata::Int64>(value));
    case type::TypeCode::Float32:
        return static_cast<double>(rtdata::fromSlot<rtdata::Float32>(value));
    case type::TypeCode::Float64:
        return rtdata::fromSlot<rtdata::Float64>(value);
    case type::TypeCode::Bool:
        return rtdata::fromSlot<rtdata::Bool>(value) ? 1.0 : 0.0;
    default:
        throw std::invalid_argument("Unsupported scalar type for Tensor operation");
    }
}

int64_t scalarToInt64(type::TypeCode code, slot_t value) {
    switch (code) {
    case type::TypeCode::Int32:
        return static_cast<int64_t>(rtdata::fromSlot<rtdata::Int32>(value));
    case type::TypeCode::Int64:
        return rtdata::fromSlot<rtdata::Int64>(value);
    case type::TypeCode::Float32:
        return static_cast<int64_t>(rtdata::fromSlot<rtdata::Float32>(value));
    case type::TypeCode::Float64:
        return static_cast<int64_t>(rtdata::fromSlot<rtdata::Float64>(value));
    case type::TypeCode::Bool:
        return rtdata::fromSlot<rtdata::Bool>(value) ? 1 : 0;
    default:
        throw std::invalid_argument("Unsupported scalar type for Tensor operation");
    }
}

bool scalarToBool(type::TypeCode code, slot_t value) {
    switch (code) {
    case type::TypeCode::Bool:
        return rtdata::fromSlot<rtdata::Bool>(value);
    case type::TypeCode::Int32:
        return rtdata::fromSlot<rtdata::Int32>(value) != 0;
    case type::TypeCode::Int64:
        return rtdata::fromSlot<rtdata::Int64>(value) != 0;
    case type::TypeCode::Float32:
        return rtdata::fromSlot<rtdata::Float32>(value) != 0.0f;
    case type::TypeCode::Float64:
        return rtdata::fromSlot<rtdata::Float64>(value) != 0.0;
    default:
        throw std::invalid_argument("Unsupported scalar type for Tensor operation");
    }
}

std::vector<int64_t> parseIntArray(const Array *array, const type::Type *arrayType) {
    if (!array || !arrayType || arrayType->code() != TypeCode::Array) {
        throw std::invalid_argument("Expected an integer array");
    }
    auto *typed = static_cast<const type::ArrayType *>(arrayType);
    if (typed->elemTypeCode() != TypeCode::Int32 && typed->elemTypeCode() != TypeCode::Int64) {
        throw std::invalid_argument("Expected an integer array");
    }
    std::vector<int64_t> values;
    values.reserve(array->size());
    for (size_t i = 0; i < array->size(); ++i) {
        values.push_back(
            typed->elemTypeCode() == TypeCode::Int32 ? static_cast<int64_t>(array->get<Int32>(i))
                                                     : array->get<Int64>(i));
    }
    return values;
}

Shape parseShapeArray(const Array *shapeArray, const type::Type *shapeType) {
    Shape shape = parseIntArray(shapeArray, shapeType);
    for (int64_t extent : shape) {
        if (extent < 0) {
            throw std::invalid_argument("Shape cannot contain negative values");
        }
    }
    return shape;
}

Array *makeShapeArray(const TensorObject *tensor, mm::IAllocator &allocator) {
    Array *shape = Array::create(allocator, tensor->rank());
    for (size_t i = 0; i < tensor->rank(); ++i) {
        shape->set(i, static_cast<rtdata::Int64>(tensor->dim(i)));
    }
    return shape;
}

TensorObject *tensorFromArray(slot_t slot, type::Type *valueType, mm::IAllocator &allocator) {
    if (valueType->code() != type::TypeCode::Array) {
        throw std::invalid_argument("Only array values can be converted to Tensor");
    }
    ArrayInference inferred = inferArrayType(slot, valueType);
    TensorObject *tensor = TensorObject::create(inferred.dtype, inferred.shape, allocator, false);
    auto *array          = rtdata::fromSlot<Array *>(slot);
    uint64_t index       = 0;
    flattenArray(array, valueType, tensor, index);
    return tensor;
}

DLManagedTensor *tensorToDLPackCopy(const TensorObject *tensor) {
    auto *managed        = new DLManagedTensor{};
    auto *ctx            = new DLPackCopyContext{};
    ctx->shape           = new int64_t[tensor->rank()];
    ctx->data            = ::operator new(tensor->byteSize());
    managed->manager_ctx = ctx;
    managed->deleter     = [](DLManagedTensor *self) {
        if (!self) {
            return;
        }
        auto *inner = static_cast<DLPackCopyContext *>(self->manager_ctx);
        delete[] inner->shape;
        ::operator delete(inner->data);
        delete inner;
        delete self;
    };
    std::memcpy(ctx->shape, tensor->shape(), tensor->rank() * sizeof(int64_t));
    std::memcpy(ctx->data, tensor->rawData(), tensor->byteSize());
    managed->dl_tensor.data        = ctx->data;
    managed->dl_tensor.device      = DLDevice{kDLCPU, 0};
    managed->dl_tensor.ndim        = static_cast<int32_t>(tensor->rank());
    managed->dl_tensor.dtype       = toDLPackType(tensor->dtype());
    managed->dl_tensor.shape       = ctx->shape;
    managed->dl_tensor.strides     = nullptr;
    managed->dl_tensor.byte_offset = 0;
    return managed;
}

TensorObject *tensorFromDLPackCopy(const DLManagedTensor *managed, mm::IAllocator &allocator) {
    if (!managed) {
        throw std::invalid_argument("Null DLPack tensor");
    }
    if (managed->dl_tensor.device.device_type != kDLCPU) {
        throw std::invalid_argument("Only CPU DLPack tensors are supported");
    }
    if (managed->dl_tensor.strides != nullptr || managed->dl_tensor.byte_offset != 0) {
        throw std::invalid_argument("Only contiguous DLPack tensors are supported");
    }
    type::TypeCode dtype = fromDLPackType(managed->dl_tensor.dtype);
    std::vector<int64_t> shape(
        managed->dl_tensor.shape,
        managed->dl_tensor.shape + managed->dl_tensor.ndim);
    TensorObject *tensor = TensorObject::create(dtype, shape, allocator, false);
    std::memcpy(tensor->rawData(), managed->dl_tensor.data, tensor->byteSize());
    return tensor;
}

} // namespace camel::tensor
