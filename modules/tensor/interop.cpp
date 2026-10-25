// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Tensor interop conversions (see interop.h).
 */

#include "interop.h"

#include "camel/core/rtdata/half.h"
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
        type::Type *elemType = arrayType->elemType();
        type::TypeCode inferredDType;
        if (elemType->code() == type::TypeCode::Array) {
            // An empty nested array has no element to take a type from; like `zeros` and `ones`,
            // a constructor that names no element type builds f32.
            inferredDType = type::TypeCode::Float32;
        } else {
            inferredDType = arrayType->elemTypeCode();
        }

        // The shape the value itself has: a rank-2 array (`f32[2, 2]`) is one array of that
        // shape, and reading its first extent alone would make a tensor of its first row
        // (design principles 5.4). A rank-1 value gives the one extent it always did, and the
        // rows of an array of arrays extend the shape below.
        std::vector<int64_t> ownShape;
        if (array == nullptr) {
            ownShape.push_back(0);
        } else {
            const std::span<const int64_t> shape = array->shapeSpan();
            ownShape.assign(shape.begin(), shape.end());
        }
        ArrayInference inferred{inferredDType, std::move(ownShape)};
        if (!array || array->numel() == 0) {
            return inferred;
        }

        if (elemType->code() == type::TypeCode::Array) {
            ArrayInference child = inferArrayType(array->getSlot(0), elemType);
            inferred.dtype       = child.dtype;
            inferred.shape.insert(inferred.shape.end(), child.shape.begin(), child.shape.end());
            for (size_t i = 1; i < array->numel(); ++i) {
                ArrayInference another = inferArrayType(array->getSlot(i), elemType);
                if (another.shape != child.shape || another.dtype != child.dtype) {
                    throw std::invalid_argument("Nested array shape or dtype mismatch");
                }
            }
        } else {
            inferred.dtype = elemType->code();
        }
        return inferred;
    }

    if (!isSupportedTensorScalar(valueType->code())) {
        throw std::invalid_argument("Only numeric arrays can be converted to Tensor");
    }
    return ArrayInference{valueType->code(), {}};
}

void flattenArray(const Array *array, type::Type *valueType, Array *tensor, uint64_t &offset) {
    auto *arrayType      = static_cast<type::ArrayType *>(valueType);
    type::Type *elemType = arrayType->elemType();
    // Every element of the value, in the order it holds them: a rank-2 array's four elements are
    // four, not the two its first extent names (design principles 5.4).
    for (size_t i = 0; i < array->numel(); ++i) {
        slot_t value = array->getSlot(i);
        if (elemType->code() == type::TypeCode::Array) {
            auto *child = rtdata::fromSlot<Array *>(value);
            flattenArray(child, elemType, tensor, offset);
            continue;
        }

        switch (tensor->dtype()) {
        case type::TypeCode::Float32:
        case type::TypeCode::Float64:
        // A bf16 or f16 element is written by the same double conversion, which rounds once into
        // it (camel/core/rtdata/half.h).
        case type::TypeCode::BFloat16:
        case type::TypeCode::Float16:
            tensor->setFromDouble(offset++, scalarToDouble(elemType->code(), value));
            break;
        case type::TypeCode::Int32:
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
    // The element types the tensor module stores as doubles: a bf16 or f16 scalar is read as the
    // f32 it widens to, exactly, so an array of one becomes a tensor of it with no second rounding
    // (`new(bf16_array)`; design principles 7.3, camel/core/rtdata/half.h).
    case type::TypeCode::BFloat16:
        return static_cast<double>(rtdata::halfToFloat<rtdata::BFloat16>(value));
    case type::TypeCode::Float16:
        return static_cast<double>(rtdata::halfToFloat<rtdata::Float16>(value));
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
    case type::TypeCode::BFloat16:
        return static_cast<int64_t>(rtdata::halfToFloat<rtdata::BFloat16>(value));
    case type::TypeCode::Float16:
        return static_cast<int64_t>(rtdata::halfToFloat<rtdata::Float16>(value));
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
    case type::TypeCode::BFloat16:
        return rtdata::halfToFloat<rtdata::BFloat16>(value) != 0.0f;
    case type::TypeCode::Float16:
        return rtdata::halfToFloat<rtdata::Float16>(value) != 0.0f;
    default:
        throw std::invalid_argument("Unsupported scalar type for Tensor operation");
    }
}

std::vector<int64_t> parseIntArray(const Array *array, const type::Type *arrayType) {
    if (!array || !arrayType || arrayType->code() != TypeCode::Array) {
        throw std::invalid_argument("Expected an integer array");
    }
    auto *typed       = static_cast<const type::ArrayType *>(arrayType);
    TypeCode elemCode = typed->elemTypeCode();
    if (elemCode == TypeCode::Void) {
        if (array->size() == 0) {
            return {}; // `[]`: no element to take a type from
        }
        // The type states no element type (`Tensor`, the numeric-unknown array: void means "a
        // numeric scalar, which one is not fixed yet", design principles 5.4), so the type
        // cannot say whether this is an integer list. The value can: read its own element type
        // and refuse here when it is not integer, instead of refusing the unstated spelling.
        elemCode = array->dtype();
    }
    if (elemCode != TypeCode::Int32 && elemCode != TypeCode::Int64) {
        throw std::invalid_argument("Expected an integer array");
    }
    std::vector<int64_t> values;
    values.reserve(array->size());
    for (size_t i = 0; i < array->size(); ++i) {
        values.push_back(
            elemCode == TypeCode::Int32 ? static_cast<int64_t>(array->get<Int32>(i))
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

Array *makeShapeArray(const Array *tensor, mm::IAllocator &allocator) {
    Array *shape =
        Array::create(allocator, type::ArrayType::create(type::Type::Int64()), tensor->rank());
    for (size_t i = 0; i < tensor->rank(); ++i) {
        shape->set(i, static_cast<rtdata::Int64>(tensor->dim(i)));
    }
    return shape;
}

Array *tensorFromArray(slot_t slot, type::Type *valueType, mm::IAllocator &allocator) {
    if (valueType->code() != type::TypeCode::Array) {
        throw std::invalid_argument("Only array values can be converted to Tensor");
    }
    ArrayInference inferred = inferArrayType(slot, valueType);
    Array *tensor           = createTensor(inferred.dtype, inferred.shape, allocator, false);
    auto *array             = rtdata::fromSlot<Array *>(slot);
    uint64_t index          = 0;
    flattenArray(array, valueType, tensor, index);
    return tensor;
}

} // namespace camel::tensor
