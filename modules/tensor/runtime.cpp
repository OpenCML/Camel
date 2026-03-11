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
 * Updated: Mar. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "runtime.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/type/composite/array.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>

namespace camel::tensor {

using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using namespace camel::core::rtdata;
using namespace camel::core::type;

namespace {

struct DLPackCopyContext {
    int64_t *shape;
    void *data;
};

uint64_t product(std::span<const int64_t> shape) {
    uint64_t value = 1;
    for (int64_t dim : shape) {
        if (dim < 0) {
            throw std::invalid_argument("Tensor shape cannot contain negative dimensions");
        }
        value *= static_cast<uint64_t>(dim);
    }
    return value;
}

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

double applyBinary(BinaryOp op, double lhs, double rhs) {
    switch (op) {
    case BinaryOp::Add:
        return lhs + rhs;
    case BinaryOp::Subtract:
        return lhs - rhs;
    case BinaryOp::Multiply:
        return lhs * rhs;
    case BinaryOp::Divide:
        if (rhs == 0.0) {
            throw std::invalid_argument("Division by zero");
        }
        return lhs / rhs;
    case BinaryOp::Power:
        return std::pow(lhs, rhs);
    }
    return 0.0;
}

bool applyCompare(CompareOp op, double lhs, double rhs) {
    switch (op) {
    case CompareOp::Less:
        return lhs < rhs;
    case CompareOp::LessEqual:
        return lhs <= rhs;
    case CompareOp::Greater:
        return lhs > rhs;
    case CompareOp::GreaterEqual:
        return lhs >= rhs;
    case CompareOp::Equal:
        return lhs == rhs;
    }
    return false;
}

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

} // namespace

TensorObject::TensorObject(type::TypeCode dtype, uint32_t rank, uint64_t numel, uint64_t byteSize)
    : dtype_(dtype), rank_(rank), numel_(numel), byteSize_(byteSize), shape_(nullptr),
      data_(nullptr) {}

TensorObject *TensorObject::create(
    type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator,
    bool zeroInit) {
    dtype              = normalizeTensorDType(dtype);
    size_t shapeBytes  = shape.size() * sizeof(int64_t);
    uint64_t numel     = product(shape);
    uint64_t dataBytes = numel * elementSize(dtype);
    size_t totalSize   = sizeof(TensorObject) + shapeBytes + static_cast<size_t>(dataBytes);
    void *memory       = allocator.alloc(totalSize, alignof(TensorObject));
    if (!memory) {
        throw std::bad_alloc();
    }

    auto *tensor =
        new (memory) TensorObject(dtype, static_cast<uint32_t>(shape.size()), numel, dataBytes);
    tensor->refreshPointers();
    if (!shape.empty()) {
        std::memcpy(tensor->shape_, shape.data(), shapeBytes);
    }
    if (zeroInit && dataBytes > 0) {
        std::memset(tensor->data_, 0, static_cast<size_t>(dataBytes));
    }
    return tensor;
}

int64_t TensorObject::dim(size_t index) const {
    ASSERT(index < rank_, "Tensor dimension out of range");
    return shape_[index];
}

bool TensorObject::sameShape(const TensorObject *other) const {
    if (!other || rank_ != other->rank_) {
        return false;
    }
    for (size_t i = 0; i < rank_; ++i) {
        if (shape_[i] != other->shape_[i]) {
            return false;
        }
    }
    return true;
}

double TensorObject::getAsDouble(uint64_t flatIndex) const {
    ASSERT(flatIndex < numel_, "Tensor flat index out of range");
    switch (dtype_) {
    case type::TypeCode::Float32:
        return static_cast<double>(dataAs<float>()[flatIndex]);
    case type::TypeCode::Int64:
        return static_cast<double>(dataAs<int64_t>()[flatIndex]);
    case type::TypeCode::Bool:
        return dataAs<uint8_t>()[flatIndex] != 0 ? 1.0 : 0.0;
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
}

int64_t TensorObject::getAsInt64(uint64_t flatIndex) const {
    ASSERT(flatIndex < numel_, "Tensor flat index out of range");
    switch (dtype_) {
    case type::TypeCode::Float32:
        return static_cast<int64_t>(dataAs<float>()[flatIndex]);
    case type::TypeCode::Int64:
        return dataAs<int64_t>()[flatIndex];
    case type::TypeCode::Bool:
        return dataAs<uint8_t>()[flatIndex] != 0 ? 1 : 0;
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
}

bool TensorObject::getAsBool(uint64_t flatIndex) const {
    ASSERT(flatIndex < numel_, "Tensor flat index out of range");
    switch (dtype_) {
    case type::TypeCode::Float32:
        return dataAs<float>()[flatIndex] != 0.0f;
    case type::TypeCode::Int64:
        return dataAs<int64_t>()[flatIndex] != 0;
    case type::TypeCode::Bool:
        return dataAs<uint8_t>()[flatIndex] != 0;
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
}

void TensorObject::setFromDouble(uint64_t flatIndex, double value) {
    ASSERT(flatIndex < numel_, "Tensor flat index out of range");
    switch (dtype_) {
    case type::TypeCode::Float32:
        dataAs<float>()[flatIndex] = static_cast<float>(value);
        break;
    case type::TypeCode::Int64:
        dataAs<int64_t>()[flatIndex] = static_cast<int64_t>(value);
        break;
    case type::TypeCode::Bool:
        dataAs<uint8_t>()[flatIndex] = value != 0.0 ? 1 : 0;
        break;
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
}

void TensorObject::setFromInt64(uint64_t flatIndex, int64_t value) {
    ASSERT(flatIndex < numel_, "Tensor flat index out of range");
    switch (dtype_) {
    case type::TypeCode::Float32:
        dataAs<float>()[flatIndex] = static_cast<float>(value);
        break;
    case type::TypeCode::Int64:
        dataAs<int64_t>()[flatIndex] = value;
        break;
    case type::TypeCode::Bool:
        dataAs<uint8_t>()[flatIndex] = value != 0 ? 1 : 0;
        break;
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
}

void TensorObject::setFromBool(uint64_t flatIndex, bool value) {
    ASSERT(flatIndex < numel_, "Tensor flat index out of range");
    switch (dtype_) {
    case type::TypeCode::Float32:
        dataAs<float>()[flatIndex] = value ? 1.0f : 0.0f;
        break;
    case type::TypeCode::Int64:
        dataAs<int64_t>()[flatIndex] = value ? 1 : 0;
        break;
    case type::TypeCode::Bool:
        dataAs<uint8_t>()[flatIndex] = value ? 1 : 0;
        break;
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
}

bool TensorObject::equals(
    const rtdata::Object *other, const type::Type *typeInfo, bool deep) const {
    (void)typeInfo;
    (void)deep;
    if (this == other) {
        return true;
    }
    if (!rtdata::isOfSameCls(this, other)) {
        return false;
    }
    auto *rhs = reinterpret_cast<const TensorObject *>(other);
    return dtype_ == rhs->dtype_ && rank_ == rhs->rank_ && numel_ == rhs->numel_ &&
           std::memcmp(shape_, rhs->shape_, rank_ * sizeof(int64_t)) == 0 &&
           std::memcmp(data_, rhs->data_, static_cast<size_t>(byteSize_)) == 0;
}

rtdata::Object *
TensorObject::clone(mm::IAllocator &allocator, const type::Type *typeInfo, bool deep) const {
    (void)typeInfo;
    (void)deep;
    auto *copy =
        TensorObject::create(dtype_, std::span<const int64_t>(shape_, rank_), allocator, false);
    if (byteSize_ > 0) {
        std::memcpy(copy->data_, data_, static_cast<size_t>(byteSize_));
    }
    return copy;
}

void TensorObject::formatElement(std::ostream &os, uint64_t flatIndex) const {
    os << formatElementToString(flatIndex);
}

std::string TensorObject::formatElementToString(uint64_t flatIndex) const {
    char buf[32];
    std::ostringstream oss;
    switch (dtype_) {
    case type::TypeCode::Float32: {
        double v = getAsDouble(flatIndex);
        // PyTorch-style: compact for int-like, 4 sigfigs for decimals
        if (v == std::floor(v) && std::abs(v) < 1e10) {
            (void)std::snprintf(buf, sizeof(buf), "% 7.0f.", v);
        } else {
            (void)std::snprintf(buf, sizeof(buf), "% 7.4g", v);
        }
        oss << buf;
        break;
    }
    case type::TypeCode::Int64:
        oss << std::setw(7) << getAsInt64(flatIndex);
        break;
    case type::TypeCode::Bool:
        oss << (getAsBool(flatIndex) ? "  true" : " false");
        break;
    default:
        oss << "?";
        break;
    }
    return oss.str();
}

void TensorObject::print(std::ostream &os, const type::Type *typeInfo) const {
    (void)typeInfo;
    os << "tensor(";
    uint64_t flatIndex = 0;
    printRecursive(os, 0, flatIndex, "");
    os << ", dtype=";
    switch (dtype_) {
    case type::TypeCode::Float32:
        os << "float32";
        break;
    case type::TypeCode::Int64:
        os << "int64";
        break;
    case type::TypeCode::Bool:
        os << "bool";
        break;
    default:
        os << "unknown";
        break;
    }
    os << ")";
}

void TensorObject::onMoved() { refreshPointers(); }

void TensorObject::updateRefs(
    const std::function<rtdata::Object *(rtdata::Object *)> &relocate, const type::Type *typeInfo) {
    (void)relocate;
    (void)typeInfo;
}

void TensorObject::refreshPointers() {
    shape_ = reinterpret_cast<int64_t *>(storage_);
    data_  = storage_ + rank_ * sizeof(int64_t);
}

void TensorObject::printRecursive(
    std::ostream &os, size_t dimIndex, uint64_t &flatIndex, const std::string &indent) const {
    constexpr int indentStep      = 8;
    constexpr size_t maxLineWidth = 100;
    const std::string innerIndent = indent + std::string(indentStep, ' ');

    if (rank_ == 0) {
        formatElement(os, 0);
        return;
    }
    if (dimIndex + 1 == rank_) {
        // Innermost dimension: wrap long rows to avoid unreadable single-line output.
        os << "[";
        size_t currentLineWidth = indent.size() + indentStep;
        for (int64_t i = 0; i < shape_[dimIndex]; ++i) {
            std::string element = formatElementToString(flatIndex++);
            if (i > 0) {
                size_t nextWidth = currentLineWidth + 2 + element.size();
                if (nextWidth > maxLineWidth) {
                    os << ",\n" << innerIndent;
                    currentLineWidth = innerIndent.size();
                } else {
                    os << ", ";
                    currentLineWidth += 2;
                }
            }
            os << element;
            currentLineWidth += element.size();
        }
        os << "]";
        return;
    }

    // Outer dimensions: each slice on new line (PyTorch-style)
    os << "[";
    for (int64_t i = 0; i < shape_[dimIndex]; ++i) {
        if (i > 0)
            os << ",\n" << innerIndent;
        printRecursive(os, dimIndex + 1, flatIndex, innerIndent);
    }
    os << "]";
}

size_t elementSize(type::TypeCode dtype) {
    switch (dtype) {
    case type::TypeCode::Float32:
        return sizeof(float);
    case type::TypeCode::Int64:
        return sizeof(int64_t);
    case type::TypeCode::Bool:
        return sizeof(uint8_t);
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
}

bool isSupportedTensorScalar(type::TypeCode code) {
    return code == type::TypeCode::Int32 || code == type::TypeCode::Int64 ||
           code == type::TypeCode::Float32 || code == type::TypeCode::Float64 ||
           code == type::TypeCode::Bool;
}

bool isFloatingTensorType(type::TypeCode code) {
    return code == type::TypeCode::Float32 || code == type::TypeCode::Float64;
}

type::TypeCode normalizeTensorDType(type::TypeCode code) {
    switch (code) {
    case type::TypeCode::Float32:
    case type::TypeCode::Float64:
        return type::TypeCode::Float32;
    case type::TypeCode::Int32:
    case type::TypeCode::Int64:
        return type::TypeCode::Int64;
    case type::TypeCode::Bool:
        return type::TypeCode::Bool;
    default:
        throw std::invalid_argument("Unsupported Tensor dtype");
    }
}

type::TypeCode tensorDTypeFromValueType(type::TypeCode code) { return normalizeTensorDType(code); }

type::TypeCode promoteTensorTypes(type::TypeCode lhs, type::TypeCode rhs) {
    lhs = normalizeTensorDType(lhs);
    rhs = normalizeTensorDType(rhs);
    if (lhs == type::TypeCode::Float32 || rhs == type::TypeCode::Float32) {
        return type::TypeCode::Float32;
    }
    if (lhs == type::TypeCode::Int64 || rhs == type::TypeCode::Int64) {
        return type::TypeCode::Int64;
    }
    return type::TypeCode::Bool;
}

std::vector<int64_t> parseShapeArray(const Array *shapeArray, const type::Type *shapeType) {
    if (!shapeArray || !shapeType || shapeType->code() != type::TypeCode::Array) {
        throw std::invalid_argument("Shape must be an integer array");
    }
    auto *arrayType = static_cast<const type::ArrayType *>(shapeType);
    if (arrayType->elemTypeCode() != type::TypeCode::Int32 &&
        arrayType->elemTypeCode() != type::TypeCode::Int64) {
        throw std::invalid_argument("Shape array must contain integers");
    }

    std::vector<int64_t> shape;
    shape.reserve(shapeArray->size());
    for (size_t i = 0; i < shapeArray->size(); ++i) {
        int64_t value = arrayType->elemTypeCode() == type::TypeCode::Int32
                            ? static_cast<int64_t>(shapeArray->get<rtdata::Int32>(i))
                            : shapeArray->get<rtdata::Int64>(i);
        if (value < 0) {
            throw std::invalid_argument("Shape cannot contain negative values");
        }
        shape.push_back(value);
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

TensorObject *
tensorEmpty(type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    return TensorObject::create(dtype, shape, allocator, true);
}

TensorObject *
tensorZeros(type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    return TensorObject::create(dtype, shape, allocator, true);
}

TensorObject *
tensorOnes(type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    TensorObject *tensor = TensorObject::create(dtype, shape, allocator, true);
    for (uint64_t i = 0; i < tensor->numel(); ++i) {
        tensor->setFromDouble(i, 1.0);
    }
    return tensor;
}

TensorObject *tensorFull(
    type::TypeCode dtype, std::span<const int64_t> shape, double value, mm::IAllocator &allocator) {
    TensorObject *tensor = TensorObject::create(dtype, shape, allocator, true);
    for (uint64_t i = 0; i < tensor->numel(); ++i) {
        tensor->setFromDouble(i, value);
    }
    return tensor;
}

TensorObject *
tensorRandom(std::span<const int64_t> shape, double low, double high, mm::IAllocator &allocator) {
    if (low > high) {
        std::swap(low, high);
    }
    TensorObject *tensor = TensorObject::create(type::TypeCode::Float32, shape, allocator, false);
    std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> dist(static_cast<float>(low), static_cast<float>(high));
    for (uint64_t i = 0; i < tensor->numel(); ++i) {
        tensor->dataAs<float>()[i] = dist(rng);
    }
    return tensor;
}

TensorObject *tensorArange(int64_t start, int64_t stop, int64_t step, mm::IAllocator &allocator) {
    if (step == 0) {
        throw std::invalid_argument("range step cannot be zero");
    }
    std::vector<int64_t> values;
    if (step > 0) {
        for (int64_t value = start; value < stop; value += step) {
            values.push_back(value);
        }
    } else {
        for (int64_t value = start; value > stop; value += step) {
            values.push_back(value);
        }
    }
    int64_t shape[]   = {static_cast<int64_t>(values.size())};
    TensorObject *out = TensorObject::create(type::TypeCode::Int64, shape, allocator, false);
    auto *buffer      = out->dataAs<int64_t>();
    for (size_t i = 0; i < values.size(); ++i) {
        buffer[i] = values[i];
    }
    return out;
}

TensorObject *tensorEye(int64_t size, mm::IAllocator &allocator) {
    if (size < 0) {
        throw std::invalid_argument("eye size cannot be negative");
    }
    int64_t shape[]   = {size, size};
    TensorObject *out = TensorObject::create(type::TypeCode::Float32, shape, allocator, true);
    auto *buffer      = out->dataAs<float>();
    for (int64_t i = 0; i < size; ++i) {
        buffer[i * size + i] = 1.0f;
    }
    return out;
}

TensorObject *tensorReshape(
    const TensorObject *tensor, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    if (tensor->numel() != product(shape)) {
        throw std::invalid_argument("reshape changes tensor element count");
    }
    TensorObject *out = TensorObject::create(tensor->dtype(), shape, allocator, false);
    if (tensor->byteSize() > 0) {
        std::memcpy(out->rawData(), tensor->rawData(), tensor->byteSize());
    }
    return out;
}

TensorObject *tensorTranspose2D(const TensorObject *tensor, mm::IAllocator &allocator) {
    if (tensor->rank() != 2) {
        throw std::invalid_argument("transpose currently only supports rank-2 tensors");
    }
    int64_t shape[]   = {tensor->dim(1), tensor->dim(0)};
    TensorObject *out = TensorObject::create(tensor->dtype(), shape, allocator, false);
    int64_t rows      = tensor->dim(0);
    int64_t cols      = tensor->dim(1);
    for (int64_t i = 0; i < rows; ++i) {
        for (int64_t j = 0; j < cols; ++j) {
            uint64_t src = static_cast<uint64_t>(i * cols + j);
            uint64_t dst = static_cast<uint64_t>(j * rows + i);
            out->setFromDouble(dst, tensor->getAsDouble(src));
        }
    }
    return out;
}

TensorObject *tensorConcat(
    const TensorObject *lhs, const TensorObject *rhs, int64_t axis, mm::IAllocator &allocator) {
    if (lhs->rank() != rhs->rank()) {
        throw std::invalid_argument("concat requires tensors with the same rank");
    }
    if (axis < 0 || axis >= static_cast<int64_t>(lhs->rank())) {
        throw std::invalid_argument("concat axis out of range");
    }
    if (lhs->dtype() != rhs->dtype()) {
        throw std::invalid_argument("concat currently requires matching dtypes");
    }

    std::vector<int64_t> shape(lhs->shape(), lhs->shape() + lhs->rank());
    for (size_t i = 0; i < lhs->rank(); ++i) {
        if (static_cast<int64_t>(i) == axis) {
            continue;
        }
        if (lhs->dim(i) != rhs->dim(i)) {
            throw std::invalid_argument("concat requires equal non-axis dimensions");
        }
    }
    shape[static_cast<size_t>(axis)] += rhs->dim(static_cast<size_t>(axis));
    TensorObject *out = TensorObject::create(lhs->dtype(), shape, allocator, false);

    uint64_t inner = 1;
    for (size_t i = static_cast<size_t>(axis) + 1; i < lhs->rank(); ++i) {
        inner *= static_cast<uint64_t>(lhs->dim(i));
    }
    uint64_t outer = 1;
    for (size_t i = 0; i < static_cast<size_t>(axis); ++i) {
        outer *= static_cast<uint64_t>(lhs->dim(i));
    }
    uint64_t lhsAxis = static_cast<uint64_t>(lhs->dim(static_cast<size_t>(axis)));
    uint64_t rhsAxis = static_cast<uint64_t>(rhs->dim(static_cast<size_t>(axis)));
    size_t itemSize  = elementSize(lhs->dtype());

    for (uint64_t outerIndex = 0; outerIndex < outer; ++outerIndex) {
        uint64_t lhsOffset = outerIndex * lhsAxis * inner;
        uint64_t rhsOffset = outerIndex * rhsAxis * inner;
        uint64_t outOffset = outerIndex * (lhsAxis + rhsAxis) * inner;
        std::memcpy(
            out->rawData() + outOffset * itemSize,
            lhs->rawData() + lhsOffset * itemSize,
            static_cast<size_t>(lhsAxis * inner) * itemSize);
        std::memcpy(
            out->rawData() + (outOffset + lhsAxis * inner) * itemSize,
            rhs->rawData() + rhsOffset * itemSize,
            static_cast<size_t>(rhsAxis * inner) * itemSize);
    }

    return out;
}

TensorObject *tensorBinary(
    const TensorObject *lhs, const TensorObject *rhs, BinaryOp op, mm::IAllocator &allocator) {
    if (!lhs->sameShape(rhs)) {
        throw std::invalid_argument("Tensor binary operation requires matching shapes");
    }
    type::TypeCode outType = promoteTensorTypes(lhs->dtype(), rhs->dtype());
    if (op == BinaryOp::Divide || op == BinaryOp::Power) {
        outType = type::TypeCode::Float32;
    }
    TensorObject *out = TensorObject::create(
        outType,
        std::span<const int64_t>(lhs->shape(), lhs->rank()),
        allocator,
        false);
    for (uint64_t i = 0; i < lhs->numel(); ++i) {
        out->setFromDouble(i, applyBinary(op, lhs->getAsDouble(i), rhs->getAsDouble(i)));
    }
    return out;
}

TensorObject *tensorBinaryScalarRight(
    const TensorObject *lhs, type::TypeCode rhsType, slot_t rhs, BinaryOp op,
    mm::IAllocator &allocator) {
    type::TypeCode outType = promoteTensorTypes(lhs->dtype(), tensorDTypeFromValueType(rhsType));
    if (op == BinaryOp::Divide || op == BinaryOp::Power) {
        outType = type::TypeCode::Float32;
    }
    double rhsValue   = scalarToDouble(rhsType, rhs);
    TensorObject *out = TensorObject::create(
        outType,
        std::span<const int64_t>(lhs->shape(), lhs->rank()),
        allocator,
        false);
    for (uint64_t i = 0; i < lhs->numel(); ++i) {
        out->setFromDouble(i, applyBinary(op, lhs->getAsDouble(i), rhsValue));
    }
    return out;
}

TensorObject *tensorBinaryScalarLeft(
    type::TypeCode lhsType, slot_t lhs, const TensorObject *rhs, BinaryOp op,
    mm::IAllocator &allocator) {
    type::TypeCode outType = promoteTensorTypes(tensorDTypeFromValueType(lhsType), rhs->dtype());
    if (op == BinaryOp::Divide || op == BinaryOp::Power) {
        outType = type::TypeCode::Float32;
    }
    double lhsValue   = scalarToDouble(lhsType, lhs);
    TensorObject *out = TensorObject::create(
        outType,
        std::span<const int64_t>(rhs->shape(), rhs->rank()),
        allocator,
        false);
    for (uint64_t i = 0; i < rhs->numel(); ++i) {
        out->setFromDouble(i, applyBinary(op, lhsValue, rhs->getAsDouble(i)));
    }
    return out;
}

TensorObject *tensorCompareScalarRight(
    const TensorObject *lhs, type::TypeCode rhsType, slot_t rhs, CompareOp op,
    mm::IAllocator &allocator) {
    double rhsValue   = scalarToDouble(rhsType, rhs);
    TensorObject *out = TensorObject::create(
        type::TypeCode::Bool,
        std::span<const int64_t>(lhs->shape(), lhs->rank()),
        allocator,
        false);
    for (uint64_t i = 0; i < lhs->numel(); ++i) {
        out->setFromBool(i, applyCompare(op, lhs->getAsDouble(i), rhsValue));
    }
    return out;
}

TensorObject *tensorCompareScalarLeft(
    type::TypeCode lhsType, slot_t lhs, const TensorObject *rhs, CompareOp op,
    mm::IAllocator &allocator) {
    double lhsValue   = scalarToDouble(lhsType, lhs);
    TensorObject *out = TensorObject::create(
        type::TypeCode::Bool,
        std::span<const int64_t>(rhs->shape(), rhs->rank()),
        allocator,
        false);
    for (uint64_t i = 0; i < rhs->numel(); ++i) {
        out->setFromBool(i, applyCompare(op, lhsValue, rhs->getAsDouble(i)));
    }
    return out;
}

TensorObject *tensorCompare(
    const TensorObject *lhs, const TensorObject *rhs, CompareOp op, mm::IAllocator &allocator) {
    if (!lhs->sameShape(rhs)) {
        throw std::invalid_argument("Tensor comparison requires matching shapes");
    }
    TensorObject *out = TensorObject::create(
        type::TypeCode::Bool,
        std::span<const int64_t>(lhs->shape(), lhs->rank()),
        allocator,
        false);
    for (uint64_t i = 0; i < lhs->numel(); ++i) {
        out->setFromBool(i, applyCompare(op, lhs->getAsDouble(i), rhs->getAsDouble(i)));
    }
    return out;
}

double tensorSum(const TensorObject *tensor) {
    double value = 0.0;
    for (uint64_t i = 0; i < tensor->numel(); ++i) {
        value += tensor->getAsDouble(i);
    }
    return value;
}

TensorObject *tensorSumAxis(const TensorObject *tensor, int64_t axis, mm::IAllocator &allocator) {
    if (tensor->rank() != 2) {
        throw std::invalid_argument("sum(axis) currently only supports rank-2 tensors");
    }
    if (axis < 0) {
        axis += 2;
    }
    if (axis != 0 && axis != 1) {
        throw std::invalid_argument("sum(axis): axis must be 0 or 1 for rank-2 tensors");
    }
    int64_t rows = tensor->dim(0);
    int64_t cols = tensor->dim(1);
    TensorObject *out;
    if (axis == 0) {
        int64_t shape[] = {cols};
        out             = TensorObject::create(type::TypeCode::Float32, shape, allocator, true);
        for (int64_t j = 0; j < cols; ++j) {
            double acc = 0.0;
            for (int64_t i = 0; i < rows; ++i) {
                acc += tensor->getAsDouble(static_cast<uint64_t>(i * cols + j));
            }
            out->setFromDouble(static_cast<uint64_t>(j), acc);
        }
    } else {
        int64_t shape[] = {rows};
        out             = TensorObject::create(type::TypeCode::Float32, shape, allocator, true);
        for (int64_t i = 0; i < rows; ++i) {
            double acc = 0.0;
            for (int64_t j = 0; j < cols; ++j) {
                acc += tensor->getAsDouble(static_cast<uint64_t>(i * cols + j));
            }
            out->setFromDouble(static_cast<uint64_t>(i), acc);
        }
    }
    return out;
}

TensorObject *tensorMaxAxis(const TensorObject *tensor, int64_t axis, mm::IAllocator &allocator) {
    if (tensor->rank() != 2) {
        throw std::invalid_argument("max(axis) currently only supports rank-2 tensors");
    }
    if (axis < 0) {
        axis += 2;
    }
    if (axis != 0 && axis != 1) {
        throw std::invalid_argument("max(axis): axis must be 0 or 1 for rank-2 tensors");
    }
    int64_t rows = tensor->dim(0);
    int64_t cols = tensor->dim(1);
    TensorObject *out;
    if (axis == 0) {
        int64_t shape[] = {cols};
        out             = TensorObject::create(type::TypeCode::Float32, shape, allocator, true);
        for (int64_t j = 0; j < cols; ++j) {
            double best = tensor->getAsDouble(static_cast<uint64_t>(j));
            for (int64_t i = 1; i < rows; ++i) {
                double v = tensor->getAsDouble(static_cast<uint64_t>(i * cols + j));
                if (v > best)
                    best = v;
            }
            out->setFromDouble(static_cast<uint64_t>(j), best);
        }
    } else {
        int64_t shape[] = {rows};
        out             = TensorObject::create(type::TypeCode::Float32, shape, allocator, true);
        for (int64_t i = 0; i < rows; ++i) {
            double best = tensor->getAsDouble(static_cast<uint64_t>(i * cols));
            for (int64_t j = 1; j < cols; ++j) {
                double v = tensor->getAsDouble(static_cast<uint64_t>(i * cols + j));
                if (v > best)
                    best = v;
            }
            out->setFromDouble(static_cast<uint64_t>(i), best);
        }
    }
    return out;
}

TensorObject *
tensorArgmaxAxis(const TensorObject *tensor, int64_t axis, mm::IAllocator &allocator) {
    if (tensor->rank() != 2) {
        throw std::invalid_argument("argmax(axis) currently only supports rank-2 tensors");
    }
    if (axis < 0) {
        axis += 2;
    }
    if (axis != 0 && axis != 1) {
        throw std::invalid_argument("argmax(axis): axis must be 0 or 1 for rank-2 tensors");
    }
    int64_t rows = tensor->dim(0);
    int64_t cols = tensor->dim(1);
    TensorObject *out;
    if (axis == 0) {
        int64_t shape[] = {cols};
        out             = TensorObject::create(type::TypeCode::Int64, shape, allocator, true);
        for (int64_t j = 0; j < cols; ++j) {
            int64_t bestIdx = 0;
            double best     = tensor->getAsDouble(static_cast<uint64_t>(j));
            for (int64_t i = 1; i < rows; ++i) {
                double v = tensor->getAsDouble(static_cast<uint64_t>(i * cols + j));
                if (v > best) {
                    best    = v;
                    bestIdx = i;
                }
            }
            out->setFromInt64(static_cast<uint64_t>(j), bestIdx);
        }
    } else {
        int64_t shape[] = {rows};
        out             = TensorObject::create(type::TypeCode::Int64, shape, allocator, true);
        for (int64_t i = 0; i < rows; ++i) {
            int64_t bestIdx = 0;
            double best     = tensor->getAsDouble(static_cast<uint64_t>(i * cols));
            for (int64_t j = 1; j < cols; ++j) {
                double v = tensor->getAsDouble(static_cast<uint64_t>(i * cols + j));
                if (v > best) {
                    best    = v;
                    bestIdx = j;
                }
            }
            out->setFromInt64(static_cast<uint64_t>(i), bestIdx);
        }
    }
    return out;
}

TensorObject *tensorExp(const TensorObject *tensor, mm::IAllocator &allocator) {
    if (!isFloatingTensorType(tensor->dtype())) {
        throw std::invalid_argument("exp requires floating-point tensor");
    }
    TensorObject *out = TensorObject::create(
        type::TypeCode::Float32,
        std::span<const int64_t>(tensor->shape(), tensor->rank()),
        allocator,
        false);
    for (uint64_t i = 0; i < tensor->numel(); ++i) {
        out->setFromDouble(i, std::exp(tensor->getAsDouble(i)));
    }
    return out;
}

TensorObject *tensorLog(const TensorObject *tensor, mm::IAllocator &allocator) {
    if (!isFloatingTensorType(tensor->dtype())) {
        throw std::invalid_argument("log requires floating-point tensor");
    }
    TensorObject *out = TensorObject::create(
        type::TypeCode::Float32,
        std::span<const int64_t>(tensor->shape(), tensor->rank()),
        allocator,
        false);
    for (uint64_t i = 0; i < tensor->numel(); ++i) {
        double v = tensor->getAsDouble(i);
        if (v <= 0.0) {
            throw std::invalid_argument("log: input must be positive");
        }
        out->setFromDouble(i, std::log(v));
    }
    return out;
}

double tensorIndex1D(const TensorObject *tensor, int64_t index) {
    if (tensor->rank() != 1) {
        throw std::invalid_argument("Single-index tensor access requires rank-1 tensor");
    }
    if (index < 0 || index >= tensor->dim(0)) {
        throw std::invalid_argument("Tensor index out of bounds");
    }
    return tensor->getAsDouble(static_cast<uint64_t>(index));
}

double tensorIndex2D(const TensorObject *tensor, int64_t row, int64_t col) {
    if (tensor->rank() != 2) {
        throw std::invalid_argument("Two-index tensor access requires rank-2 tensor");
    }
    if (row < 0 || row >= tensor->dim(0) || col < 0 || col >= tensor->dim(1)) {
        throw std::invalid_argument("Tensor index out of bounds");
    }
    return tensor->getAsDouble(static_cast<uint64_t>(row * tensor->dim(1) + col));
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
