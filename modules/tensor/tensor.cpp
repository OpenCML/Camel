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
 * TensorObject layout, element access, printing, and GC hooks.
 */

#include "tensor.h"

#include "camel/core/mm.h"
#include "camel/utils/assert.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace camel::tensor {

using type::TypeCode;

uint64_t numelOf(std::span<const int64_t> shape) {
    uint64_t total = 1;
    for (int64_t extent : shape) {
        if (extent < 0) {
            throw std::invalid_argument("Tensor shape cannot contain negative extents");
        }
        total *= static_cast<uint64_t>(extent);
    }
    return total;
}

TensorObject::TensorObject(type::TypeCode dtype, uint32_t rank, uint64_t numel, uint64_t byteSize)
    : dtype_(dtype), rank_(rank), numel_(numel), byteSize_(byteSize), shape_(nullptr),
      data_(nullptr) {}

TensorObject *TensorObject::create(
    type::TypeCode dtype, std::span<const int64_t> shape, mm::IAllocator &allocator,
    bool zeroInit) {
    dtype              = normalizeTensorDType(dtype);
    size_t shapeBytes  = shape.size() * sizeof(int64_t);
    uint64_t numel     = numelOf(shape);
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

TensorObject *TensorObject::createView(
    const TensorObject *source, std::span<const int64_t> shape, mm::IAllocator &allocator) {
    if (numelOf(shape) != source->numel_) {
        throw std::invalid_argument("Tensor view must keep the element count");
    }
    if (&allocator != &mm::autoSpace()) {
        TensorObject *copy = create(source->dtype_, shape, allocator, false);
        if (source->byteSize_ > 0) {
            std::memcpy(copy->data_, source->data_, static_cast<size_t>(source->byteSize_));
        }
        return copy;
    }
    const TensorObject *owner = source->owner_ ? source->owner_ : source;
    const size_t shapeBytes   = shape.size() * sizeof(int64_t);
    void *memory = allocator.alloc(sizeof(TensorObject) + shapeBytes, alignof(TensorObject));
    if (!memory) {
        throw std::bad_alloc();
    }
    auto *view = new (memory) TensorObject(
        source->dtype_,
        static_cast<uint32_t>(shape.size()),
        source->numel_,
        source->byteSize_);
    view->owner_           = const_cast<TensorObject *>(owner);
    view->ownerDataOffset_ = static_cast<size_t>(
        owner->data_ - reinterpret_cast<const std::byte *>(owner));
    view->refreshPointers();
    if (!shape.empty()) {
        std::memcpy(view->shape_, shape.data(), shapeBytes);
    }
    return view;
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
    os << ", dtype=" << dtypeName(dtype_);
    os << ")";
}

void TensorObject::onMoved() { refreshPointers(); }

void TensorObject::updateRefs(
    const rtdata::Object::RefRelocator &relocate, const type::Type *typeInfo) {
    if (!owner_) {
        return;
    }
    const rtdata::RefTraceInfo info{
        .owner     = this,
        .ownerType = typeInfo,
        .slotType  = typeInfo,
        .ownerKind = "TensorObject",
        .slotName  = "owner",
    };
    owner_ = static_cast<TensorObject *>(relocate(owner_, typeInfo, info));
    refreshPointers();
}

void TensorObject::refreshPointers() {
    shape_ = reinterpret_cast<int64_t *>(storage_);
    data_  = owner_ ? reinterpret_cast<std::byte *>(owner_) + ownerDataOffset_
                    : storage_ + rank_ * sizeof(int64_t);
}

void TensorObject::printRecursive(
    std::ostream &os, size_t dimIndex, uint64_t &flatIndex, const std::string &indent) const {
    constexpr int indentStep      = 8;
    constexpr size_t maxLineWidth = 100;
    const std::string innerIndent = indent + std::string(indentStep, ' ');

    if (rank_ == 0) {
        os << formatElementToString(0);
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

} // namespace camel::tensor
