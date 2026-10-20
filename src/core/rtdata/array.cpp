/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
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
 * The runtime array value: two storage classes, shapes, views and the object protocol.
 * See include/camel/core/rtdata/array.h for the representation rules.
 */

#include "camel/core/rtdata/array.h"

#include "camel/core/mm.h"
#include "camel/core/type/text.h"
#include "camel/utils/assert.h"
#include "camel/utils/number_text.h"

#include <cmath>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace {

using camel::core::type::TypeCode;

uint64_t checkedNumel(std::span<const int64_t> extents) {
    uint64_t total = 1;
    for (int64_t extent : extents) {
        if (extent < 0) {
            throw std::invalid_argument("Array shape cannot contain negative extents");
        }
        const uint64_t previous = total;
        total *= static_cast<uint64_t>(extent);
        if (extent != 0 && total / static_cast<uint64_t>(extent) != previous) {
            throw std::invalid_argument("Array shape is too large");
        }
    }
    return total;
}

/// Row-major strides, in elements.
void fillStrides(std::span<const int64_t> extents, int64_t *strides) {
    int64_t stride = 1;
    for (size_t i = extents.size(); i-- > 0;) {
        strides[i] = stride;
        stride *= extents[i];
    }
}

/// The storage name the tensor print has always used (the tensor module's dtypeName).
const char *storageName(TypeCode code) {
    switch (code) {
    case TypeCode::Float32:
        return "float32";
    case TypeCode::Float64:
        return "float64";
    case TypeCode::Int32:
        return "int32";
    case TypeCode::Int64:
        return "int64";
    case TypeCode::Bool:
        return "bool";
    case TypeCode::Byte:
        return "uint8";
    default:
        return "unknown";
    }
}

} // namespace

Array *Array::create(camel::core::mm::IAllocator &allocator, size_t initSize) {
    return createSlots(allocator, initSize, std::max(initSize, kInlineSlots));
}

Array *Array::create(
    camel::core::mm::IAllocator &allocator, const type::ArrayType *arrayType,
    std::span<const int64_t> extents, bool zeroInit) {
    const uint64_t numel = checkedNumel(extents);
    const TypeCode elemCode =
        arrayType != nullptr ? arrayType->elemTypeCode() : TypeCode::Void;
    const type::ArrayStorage storage =
        type::arrayStorageFor(arrayType != nullptr ? arrayType->elemType() : nullptr);

    if (storage == type::ArrayStorage::Slots) {
        Array *array = createSlotsRank(allocator, extents, static_cast<size_t>(numel));
        array->elemTypeCode_ = elemCode;
        return array;
    }

    return createCompact(allocator, elemCode, extents, nullptr, 0, zeroInit, false);
}

Array *Array::create(
    camel::core::mm::IAllocator &allocator, const type::ArrayType *arrayType, size_t size,
    bool zeroInit) {
    const int64_t extent = static_cast<int64_t>(size);
    return create(allocator, arrayType, std::span<const int64_t>(&extent, 1), zeroInit);
}

Array *
Array::view(Array *source, std::span<const int64_t> extents, uint64_t elementOffset) {
    ASSERT(source != nullptr, "A view needs a source array");
    ASSERT(source->isCompact(), "Only a compact numeric array can be viewed");
    const uint64_t elements = checkedNumel(extents);
    if (elementOffset > source->numel_ || elements > source->numel_ - elementOffset) {
        throw std::invalid_argument("Array view must stay within its source");
    }
    source->ensureBuffer();
    Buffer *buffer = source->buffer_;
    const size_t byteOffset =
        source->offset_ + static_cast<size_t>(elementOffset) * source->elementSize_;
    Array *view =
        createCompact(*source->allocator_, source->elemTypeCode_, extents, buffer, byteOffset,
                      false, true);
    return view;
}

Array *Array::wrap(
    camel::core::mm::IAllocator &allocator, const type::ArrayType *arrayType,
    std::span<const int64_t> extents, Buffer *buffer, size_t byteOffset, bool view) {
    ASSERT(buffer != nullptr, "An array wrap needs a buffer");
    ASSERT(arrayType != nullptr, "An array wrap needs its element type");
    ASSERT(
        type::arrayStorageFor(arrayType->elemType()) == type::ArrayStorage::Compact,
        "Only a numeric array can wrap an external buffer");
    return createCompact(
        allocator, arrayType->elemTypeCode(), extents, buffer, byteOffset, false, view);
}

Array *Array::createSlots(
    camel::core::mm::IAllocator &allocator, size_t initSize, size_t capacity) {
    const int64_t extent = static_cast<int64_t>(initSize);
    return createSlotsRank(allocator, std::span<const int64_t>(&extent, 1), capacity);
}

Array *Array::createSlotsRank(
    camel::core::mm::IAllocator &allocator, std::span<const int64_t> extents, size_t capacity) {
    const uint64_t numel = checkedNumel(extents);
    const size_t rank    = extents.size();
    const size_t total   = sizeof(Array) + rank * 2 * sizeof(int64_t);
    void *memory         = allocator.alloc(total, alignof(Array));
    if (!memory) {
        throw std::bad_alloc();
    }
    Array *array        = new (memory) Array();
    array->allocator_   = &allocator;
    array->storage_     = Storage::Slots;
    array->rank_        = static_cast<uint32_t>(rank);
    array->elementSize_ = sizeof(slot_t);
    array->extents_     = reinterpret_cast<int64_t *>(array + 1);
    array->strides_     = array->extents_ + rank;
    for (size_t i = 0; i < rank; ++i) {
        array->extents_[i] = extents[i];
    }
    fillStrides(extents, array->strides_);

    array->size_     = extents.empty() ? 0 : static_cast<uint64_t>(extents[0]);
    array->numel_    = numel;
    array->capacity_ = std::max(capacity, kInlineSlots);

    if (array->capacity_ > kInlineSlots) {
        array->fixedArray_ = FixedArray::create(array->capacity_, allocator);
        array->dataPtr_    = reinterpret_cast<std::byte *>(array->fixedArray_->data());
        std::fill(
            array->fixedArray_->data(), array->fixedArray_->data() + array->capacity_, NullSlot);
    } else {
        array->fixedArray_ = nullptr;
        array->dataPtr_    = reinterpret_cast<std::byte *>(array->inlineData_);
        std::fill(array->inlineData_, array->inlineData_ + kInlineSlots, NullSlot);
    }
    return array;
}

Array *Array::createCompact(
    camel::core::mm::IAllocator &allocator, TypeCode elemType, std::span<const int64_t> extents,
    Buffer *buffer, size_t byteOffset, bool zeroInit, bool view) {
    const uint64_t numel = checkedNumel(extents);
    const size_t elemSize = type::arrayElementSize(elemType);
    const size_t bytes    = static_cast<size_t>(numel) * elemSize;
    const size_t rank     = extents.size();
    const bool inlineData = buffer == nullptr && bytes <= camel::core::array::kInlineArrayBytes;

    size_t total = sizeof(Array) + rank * 2 * sizeof(int64_t);
    if (inlineData) {
        total += bytes;
    }
    void *memory = allocator.alloc(total, alignof(Array));
    if (!memory) {
        throw std::bad_alloc();
    }
    Array *array        = new (memory) Array();
    array->allocator_   = &allocator;
    array->storage_     = Storage::Compact;
    array->elemTypeCode_ = elemType;
    array->elementSize_ = elemSize;
    array->rank_        = static_cast<uint32_t>(rank);
    array->numel_       = numel;
    array->size_        = extents.empty() ? 0 : static_cast<uint64_t>(extents[0]);
    array->view_        = view;
    array->extents_     = reinterpret_cast<int64_t *>(array + 1);
    array->strides_     = array->extents_ + rank;
    for (size_t i = 0; i < rank; ++i) {
        array->extents_[i] = extents[i];
    }
    fillStrides(extents, array->strides_);

    if (buffer != nullptr) {
        array->buffer_ = buffer;
        buffer->retain();
        array->offset_ = byteOffset;
        // A view never grows into the source's storage: any append copies first.
        array->capacity_ = view ? numel : (elemSize == 0 ? 0 : buffer->bytes() / elemSize);
        array->dataPtr_  = buffer->data() + byteOffset;
        array->noteFinalizable();
    } else if (inlineData) {
        array->buffer_    = nullptr;
        array->capacity_  = numel;
        array->dataPtr_   = reinterpret_cast<std::byte *>(array->strides_ + rank);
        if (zeroInit && bytes > 0) {
            std::memset(array->dataPtr_, 0, bytes);
        }
    } else {
        Buffer *created = camel::core::array::Buffer::create(bytes);
        array->buffer_  = created;
        array->offset_  = 0;
        array->capacity_ = numel;
        array->dataPtr_  = created->data();
        if (zeroInit && bytes > 0) {
            std::memset(array->dataPtr_, 0, bytes);
        }
        array->noteFinalizable();
    }
    return array;
}

void Array::noteFinalizable() {
    if (buffer_ != nullptr && allocator_ != nullptr) {
        allocator_->noteFinalizable(this);
    }
}

void Array::recomputePointers() {
    extents_ = reinterpret_cast<int64_t *>(this + 1);
    strides_ = extents_ + rank_;
    if (isSlots()) {
        dataPtr_ = fixedArray_ != nullptr ? reinterpret_cast<std::byte *>(fixedArray_->data())
                                          : reinterpret_cast<std::byte *>(inlineData_);
        return;
    }
    if (buffer_ != nullptr) {
        dataPtr_ = buffer_->data() + offset_;
    } else {
        dataPtr_ = reinterpret_cast<std::byte *>(strides_ + rank_);
    }
}

void Array::onMoved() { recomputePointers(); }

bool Array::sameShape(const Array *other) const {
    if (other == nullptr || rank_ != other->rank_) {
        return false;
    }
    for (size_t i = 0; i < rank_; ++i) {
        if (extents_[i] != other->extents_[i]) {
            return false;
        }
    }
    return true;
}

slot_t Array::getSlot(uint64_t flatIndex) const {
    ASSERT(flatIndex < numel_, "Array flat index out of range");
    if (isSlots()) {
        return reinterpret_cast<const slot_t *>(dataPtr_)[flatIndex];
    }
    const std::byte *at = dataPtr_ + flatIndex * elementSize_;
    switch (elementSize_) {
    case 8: {
        slot_t value = 0;
        std::memcpy(&value, at, sizeof(value));
        return value;
    }
    case 4: {
        uint32_t value = 0;
        std::memcpy(&value, at, sizeof(value));
        return static_cast<slot_t>(value);
    }
    case 2: {
        uint16_t value = 0;
        std::memcpy(&value, at, sizeof(value));
        return static_cast<slot_t>(value);
    }
    case 1:
        return static_cast<slot_t>(*at);
    default:
        ASSERT(false, "Unsupported compact element size");
        return NullSlot;
    }
}

void Array::setSlot(uint64_t flatIndex, slot_t value, const type::ArrayType *arrayType) {
    ASSERT(flatIndex < numel_, "Array flat index out of range");
    if (isSlots()) {
        rtdata::Object *owner =
            fixedArray_ != nullptr ? static_cast<rtdata::Object *>(fixedArray_)
                                   : static_cast<rtdata::Object *>(this);
        if (arrayType != nullptr) {
            camel::core::mm::writeBarrier(owner, arrayType, value, arrayType->elemType());
        }
        reinterpret_cast<slot_t *>(dataPtr_)[flatIndex] = value;
        return;
    }
    std::byte *at = dataPtr_ + flatIndex * elementSize_;
    switch (elementSize_) {
    case 8:
        std::memcpy(at, &value, sizeof(value));
        break;
    case 4: {
        const uint32_t low = static_cast<uint32_t>(value);
        std::memcpy(at, &low, sizeof(low));
        break;
    }
    case 2: {
        const uint16_t low = static_cast<uint16_t>(value);
        std::memcpy(at, &low, sizeof(low));
        break;
    }
    case 1:
        *at = static_cast<std::byte>(elemTypeCode_ == TypeCode::Bool
                                         ? (value != 0 ? 1 : 0)
                                         : (value & 0xFF));
        break;
    default:
        ASSERT(false, "Unsupported compact element size");
        break;
    }
}

double Array::getAsDouble(uint64_t flatIndex) const {
    ASSERT(flatIndex < numel_, "Array flat index out of range");
    switch (elemTypeCode_) {
    case TypeCode::Float32:
        return static_cast<double>(dataAs<float>()[flatIndex]);
    case TypeCode::Float64:
        return dataAs<double>()[flatIndex];
    case TypeCode::Int32:
        return static_cast<double>(dataAs<int32_t>()[flatIndex]);
    case TypeCode::Int64:
        return static_cast<double>(dataAs<int64_t>()[flatIndex]);
    case TypeCode::Bool:
        return dataAs<uint8_t>()[flatIndex] != 0 ? 1.0 : 0.0;
    case TypeCode::Byte:
        return static_cast<double>(dataAs<uint8_t>()[flatIndex]);
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

int64_t Array::getAsInt64(uint64_t flatIndex) const {
    ASSERT(flatIndex < numel_, "Array flat index out of range");
    switch (elemTypeCode_) {
    case TypeCode::Float32:
        return static_cast<int64_t>(dataAs<float>()[flatIndex]);
    case TypeCode::Float64:
        return static_cast<int64_t>(dataAs<double>()[flatIndex]);
    case TypeCode::Int32:
        return dataAs<int32_t>()[flatIndex];
    case TypeCode::Int64:
        return dataAs<int64_t>()[flatIndex];
    case TypeCode::Bool:
        return dataAs<uint8_t>()[flatIndex] != 0 ? 1 : 0;
    case TypeCode::Byte:
        return dataAs<uint8_t>()[flatIndex];
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

bool Array::getAsBool(uint64_t flatIndex) const {
    ASSERT(flatIndex < numel_, "Array flat index out of range");
    switch (elemTypeCode_) {
    case TypeCode::Float32:
        return dataAs<float>()[flatIndex] != 0.0f;
    case TypeCode::Float64:
        return dataAs<double>()[flatIndex] != 0.0;
    case TypeCode::Int32:
        return dataAs<int32_t>()[flatIndex] != 0;
    case TypeCode::Int64:
        return dataAs<int64_t>()[flatIndex] != 0;
    case TypeCode::Bool:
        return dataAs<uint8_t>()[flatIndex] != 0;
    case TypeCode::Byte:
        return dataAs<uint8_t>()[flatIndex] != 0;
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

void Array::setFromDouble(uint64_t flatIndex, double value) {
    ASSERT(flatIndex < numel_, "Array flat index out of range");
    switch (elemTypeCode_) {
    case TypeCode::Float32:
        dataAs<float>()[flatIndex] = static_cast<float>(value);
        break;
    case TypeCode::Float64:
        dataAs<double>()[flatIndex] = value;
        break;
    case TypeCode::Int32:
        dataAs<int32_t>()[flatIndex] = static_cast<int32_t>(value);
        break;
    case TypeCode::Int64:
        dataAs<int64_t>()[flatIndex] = static_cast<int64_t>(value);
        break;
    case TypeCode::Bool:
        dataAs<uint8_t>()[flatIndex] = value != 0.0 ? 1 : 0;
        break;
    case TypeCode::Byte:
        dataAs<uint8_t>()[flatIndex] = static_cast<uint8_t>(value);
        break;
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

void Array::setFromInt64(uint64_t flatIndex, int64_t value) {
    ASSERT(flatIndex < numel_, "Array flat index out of range");
    switch (elemTypeCode_) {
    case TypeCode::Float32:
        dataAs<float>()[flatIndex] = static_cast<float>(value);
        break;
    case TypeCode::Float64:
        dataAs<double>()[flatIndex] = static_cast<double>(value);
        break;
    case TypeCode::Int32:
        dataAs<int32_t>()[flatIndex] = static_cast<int32_t>(value);
        break;
    case TypeCode::Int64:
        dataAs<int64_t>()[flatIndex] = value;
        break;
    case TypeCode::Bool:
        dataAs<uint8_t>()[flatIndex] = value != 0 ? 1 : 0;
        break;
    case TypeCode::Byte:
        dataAs<uint8_t>()[flatIndex] = static_cast<uint8_t>(value);
        break;
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

void Array::setFromBool(uint64_t flatIndex, bool value) {
    ASSERT(flatIndex < numel_, "Array flat index out of range");
    switch (elemTypeCode_) {
    case TypeCode::Float32:
        dataAs<float>()[flatIndex] = value ? 1.0f : 0.0f;
        break;
    case TypeCode::Float64:
        dataAs<double>()[flatIndex] = value ? 1.0 : 0.0;
        break;
    case TypeCode::Int32:
        dataAs<int32_t>()[flatIndex] = value ? 1 : 0;
        break;
    case TypeCode::Int64:
        dataAs<int64_t>()[flatIndex] = value ? 1 : 0;
        break;
    case TypeCode::Bool:
        dataAs<uint8_t>()[flatIndex] = value ? 1 : 0;
        break;
    case TypeCode::Byte:
        dataAs<uint8_t>()[flatIndex] = value ? 1 : 0;
        break;
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

void Array::reserve(size_t newCapacity) { reserve(newCapacity, nullptr); }

void Array::reserve(size_t newCapacity, const type::ArrayType *arrayType) {
    if (isSlots()) {
        if (newCapacity > capacity_) {
            reallocateSlots(newCapacity, arrayType);
        }
        return;
    }
    if (newCapacity > capacity_) {
        growCompact(newCapacity);
    }
}

void Array::resize(size_t newSize) { resize(newSize, nullptr); }

void Array::resize(size_t newSize, const type::ArrayType *arrayType) {
    if (isSlots()) {
        reserve(newSize, arrayType);
        if (newSize > size_) {
            std::fill(
                reinterpret_cast<slot_t *>(dataPtr_) + size_,
                reinterpret_cast<slot_t *>(dataPtr_) + newSize,
                NullSlot);
        }
        size_        = newSize;
        numel_       = newSize;
        extents_[0]  = static_cast<int64_t>(newSize);
        return;
    }
    if (newSize > capacity_) {
        growCompact(newSize);
    }
    if (newSize > size_) {
        std::memset(dataPtr_ + size_ * elementSize_, 0, (newSize - size_) * elementSize_);
    }
    size_       = newSize;
    numel_      = newSize;
    extents_[0] = static_cast<int64_t>(newSize);
}

void Array::reallocateSlots(size_t newCapacity, const type::ArrayType *arrayType) {
    if (newCapacity <= kInlineSlots) {
        if (fixedArray_ != nullptr && size_ > 0) {
            std::memcpy(inlineData_, dataPtr_, size_ * sizeof(slot_t));
            if (arrayType && type::isGCTraced(arrayType->elemTypeCode())) {
                for (size_t i = 0; i < size_; ++i) {
                    camel::core::mm::writeBarrier(
                        this, arrayType, inlineData_[i], arrayType->elemType());
                }
            }
        }
        fixedArray_ = nullptr;
        dataPtr_    = reinterpret_cast<std::byte *>(inlineData_);
        capacity_   = kInlineSlots;
        return;
    }
    FixedArray *newArray = FixedArray::create(newCapacity, *allocator_);
    if (size_ > 0) {
        std::memcpy(newArray->data(), dataPtr_, size_ * sizeof(slot_t));
        if (arrayType && type::isGCTraced(arrayType->elemTypeCode())) {
            for (size_t i = 0; i < size_; ++i) {
                camel::core::mm::writeBarrier(
                    newArray, arrayType, newArray->data()[i], arrayType->elemType());
            }
        }
    }
    fixedArray_ = newArray;
    dataPtr_    = reinterpret_cast<std::byte *>(newArray->data());
    capacity_   = newCapacity;
    if (arrayType) {
        camel::core::mm::writeBarrier(this, arrayType, fixedArray_, arrayType);
    }
}

void Array::growCompact(uint64_t newCapacity) {
    ASSERT(isCompact(), "Only a compact array grows this way");
    const size_t newBytes = static_cast<size_t>(newCapacity) * elementSize_;
    if (buffer_ == nullptr) {
        // Inline data: move it into a buffer, which is what makes the array growable and gives
        // views a stable address.
        Buffer *buffer = camel::core::array::Buffer::create(newBytes);
        const size_t used = byteSize();
        if (used > 0) {
            std::memcpy(buffer->data(), dataPtr_, used);
        }
        buffer_   = buffer;
        dataPtr_  = buffer->data();
        offset_   = 0;
        capacity_ = newCapacity;
        noteFinalizable();
        return;
    }
    if (buffer_->unique() && offset_ == 0 && !view_) {
        buffer_->grow(newBytes);
        dataPtr_  = buffer_->data();
        capacity_ = newCapacity;
        return;
    }
    // Shared (a view, or another array): copy to a new buffer and leave the old one to its
    // other holders.
    Buffer *buffer = camel::core::array::Buffer::create(newBytes);
    const size_t used = byteSize();
    if (used > 0) {
        std::memcpy(buffer->data(), dataPtr_, used);
    }
    Buffer *old = buffer_;
    buffer_     = buffer;
    dataPtr_    = buffer->data();
    offset_     = 0;
    capacity_   = newCapacity;
    view_       = false;
    if (old != nullptr) {
        old->release();
    }
    noteFinalizable();
}

void Array::appendSlot(slot_t value, const type::ArrayType *arrayType) {
    if (isSlots()) {
        if (size_ >= capacity_) {
            reallocateSlots(capacity_ * 3 / 2 + 1, arrayType);
        }
        rtdata::Object *owner =
            fixedArray_ != nullptr ? static_cast<rtdata::Object *>(fixedArray_)
                                   : static_cast<rtdata::Object *>(this);
        if (arrayType != nullptr) {
            camel::core::mm::writeBarrier(owner, arrayType, value, arrayType->elemType());
        }
        reinterpret_cast<slot_t *>(dataPtr_)[size_++] = value;
        ++numel_;
        extents_[0] = static_cast<int64_t>(size_);
        return;
    }
    if (size_ >= capacity_) {
        growCompact(capacity_ * 3 / 2 + 1);
    }
    setSlot(size_, value, arrayType);
    ++size_;
    ++numel_;
    extents_[0] = static_cast<int64_t>(size_);
}

void Array::shrinkToFit() { shrinkToFit(nullptr); }

void Array::shrinkToFit(const type::ArrayType *arrayType) {
    if (isSlots()) {
        if (size_ == capacity_) {
            return;
        }
        reallocateSlots(std::max<size_t>(size_, kInlineSlots), arrayType);
        return;
    }
    // Compact arrays are exact by construction; a capacity above the length only exists after
    // growth through append, which amortizes the next append.
}

Array::Buffer *Array::reusableBuffer(size_t newBytes) {
    if (isCompact() && buffer_ != nullptr && buffer_->unique() && offset_ == 0 && !view_ &&
        buffer_->bytes() >= newBytes) {
        return buffer_;
    }
    Buffer *buffer = camel::core::array::Buffer::create(newBytes);
    const size_t used = std::min(byteSize(), newBytes);
    if (used > 0 && dataPtr_ != nullptr) {
        std::memcpy(buffer->data(), dataPtr_, used);
    }
    return buffer;
}

void Array::adoptBuffer(Buffer *buffer, size_t newBytes) {
    if (buffer == nullptr || buffer == buffer_) {
        return;
    }
    ASSERT(isCompact(), "Only a compact array adopts a buffer");
    Buffer *old = buffer_;
    buffer_     = buffer;
    dataPtr_    = buffer->data();
    offset_     = 0;
    capacity_   = elementSize_ == 0 ? 0 : newBytes / elementSize_;
    view_       = false;
    if (old != nullptr) {
        old->release();
    }
    noteFinalizable();
}

bool Array::canWriteInPlace() const {
    if (!isCompact()) {
        return false;
    }
    if (buffer_ == nullptr) {
        return true; // inline: this array object is the storage, so nothing else can see it
    }
    return buffer_->unique() && buffer_->camelOwned() && offset_ == 0 && !view_;
}

void Array::ensureBuffer() {
    if (!isCompact() || buffer_ != nullptr) {
        return;
    }
    const size_t bytes = byteSize();
    Buffer *buffer     = camel::core::array::Buffer::create(bytes);
    if (bytes > 0) {
        std::memcpy(buffer->data(), dataPtr_, bytes);
    }
    buffer_   = buffer;
    dataPtr_  = buffer->data();
    offset_   = 0;
    capacity_ = numel_;
    noteFinalizable();
}

void Array::releaseBuffer() noexcept {
    if (buffer_ != nullptr) {
        Buffer *buffer = buffer_;
        buffer_        = nullptr;
        buffer->release();
    }
}

void Array::finalize() noexcept {
    if (buffer_ != nullptr) {
        Buffer *buffer = buffer_;
        buffer_        = nullptr;
        buffer->releaseFromFinalizer();
    }
}

bool Array::compactEqualsElements(const Array *other, bool deep) const {
    (void)deep;
    if (elemTypeCode_ != other->elemTypeCode_ || numel_ != other->numel_ ||
        rank_ != other->rank_) {
        return false;
    }
    for (size_t i = 0; i < rank_; ++i) {
        if (extents_[i] != other->extents_[i]) {
            return false;
        }
    }
    return byteSize() == 0 || std::memcmp(dataPtr_, other->dataPtr_, byteSize()) == 0;
}

bool Array::equals(
    const rtdata::Object *other, const type::Type *typeInfo, bool deep) const {
    if (this == other) {
        return true;
    }
    if (!rtdata::isOfSameCls(this, other)) {
        return false;
    }
    const auto *rhs = reinterpret_cast<const Array *>(other);
    if (!sameShape(rhs)) {
        return false;
    }

    const auto *arrayType =
        typeInfo != nullptr && typeInfo->code() == TypeCode::Array
            ? static_cast<const type::ArrayType *>(typeInfo)
            : nullptr;

    if (isCompact() && rhs->isCompact()) {
        return compactEqualsElements(rhs, deep);
    }

    // At least one side is slots: compare element by element through the element type.
    if (arrayType == nullptr) {
        // A non-array static type (a tensor value) on a slot array cannot happen: tensors are
        // compact. Fall back to the element code the slot array carries.
        if (elemTypeCode_ != rhs->elemTypeCode_) {
            return false;
        }
        for (uint64_t i = 0; i < numel_; ++i) {
            if (getSlot(i) != rhs->getSlot(i)) {
                return false;
            }
        }
        return true;
    }

    const type::TypeCode elemCode = arrayType->elemTypeCode();
    if (type::isGCTraced(elemCode)) {
        const type::Type *elemType = arrayType->elemType();
        for (uint64_t i = 0; i < numel_; ++i) {
            const rtdata::Object *a = rtdata::fromSlot<const rtdata::Object *>(getSlot(i));
            const rtdata::Object *b = rtdata::fromSlot<const rtdata::Object *>(rhs->getSlot(i));
            if (a == b) {
                continue;
            }
            if (a == nullptr || b == nullptr) {
                return false;
            }
            if (deep) {
                if (!a->equals(b, elemType, true)) {
                    return false;
                }
            } else {
                return false;
            }
        }
        return true;
    }
    for (uint64_t i = 0; i < numel_; ++i) {
        if (getSlot(i) != rhs->getSlot(i)) {
            return false;
        }
    }
    return true;
}

rtdata::Object *Array::clone(
    camel::core::mm::IAllocator &allocator, const type::Type *typeInfo, bool deep) const {
    if (isCompact()) {
        Array *copy = createCompact(
            allocator, elemTypeCode_, std::span<const int64_t>(extents_, rank_), nullptr, 0,
            false, false);
        if (byteSize() > 0) {
            std::memcpy(copy->dataPtr_, dataPtr_, byteSize());
        }
        return copy;
    }

    ASSERT(typeInfo && typeInfo->code() == TypeCode::Array, "Type must be ArrayType");
    const auto *arrayType            = static_cast<const type::ArrayType *>(typeInfo);
    const type::TypeCode elemTypeCode = arrayType->elemTypeCode();
    Array *copy = createSlots(allocator, size_, capacity_);
    copy->elemTypeCode_ = elemTypeCode;
    const rtdata::Object *const *srcArr =
        reinterpret_cast<const rtdata::Object *const *>(dataPtr_);

    if (type::isGCTraced(elemTypeCode) && fixedArray_ == nullptr) {
        for (size_t i = 0; i < size_; ++i) {
            const rtdata::Object *oriRef = srcArr[i];
            rtdata::Object *stored       = nullptr;
            if (oriRef != nullptr) {
                stored = deep ? oriRef->clone(allocator, arrayType->elemType(), true)
                              : const_cast<rtdata::Object *>(oriRef);
            } else {
                stored = rtdata::NullRef;
            }
            copy->inlineData_[i] = rtdata::toSlot(stored);
            camel::core::mm::writeBarrier(
                copy, arrayType, copy->inlineData_[i], arrayType->elemType());
        }
        return copy;
    }

    if (fixedArray_ != nullptr) {
        copy->fixedArray_ = static_cast<FixedArray *>(
            fixedArray_->clone(allocator, typeInfo, deep));
        copy->dataPtr_ = reinterpret_cast<std::byte *>(copy->fixedArray_->data());
        camel::core::mm::writeBarrier(copy, arrayType, copy->fixedArray_, arrayType);
    } else {
        std::memcpy(copy->inlineData_, dataPtr_, size_ * sizeof(slot_t));
        if (type::isGCTraced(elemTypeCode)) {
            for (size_t i = 0; i < size_; ++i) {
                camel::core::mm::writeBarrier(
                    copy, arrayType, copy->inlineData_[i], arrayType->elemType());
            }
        }
    }
    return copy;
}

void Array::print(std::ostream &os, const type::Type *typeInfo) const {
    if (typeInfo != nullptr && typeInfo->code() == TypeCode::Array) {
        uint64_t flatIndex = 0;
        printArray(os, static_cast<const type::ArrayType *>(typeInfo), 0, flatIndex, "");
    } else {
        printTensor(os);
    }
}

void Array::printArray(
    std::ostream &os, const type::ArrayType *arrayType, size_t dim, uint64_t &flatIndex,
    const std::string &indent) const {
    os << "[";
    const int64_t extent = extents_[dim];
    for (int64_t i = 0; i < extent; ++i) {
        if (i > 0) {
            os << ", ";
        }
        if (dim + 1 == rank_) {
            rtdata::printSlot(os, getSlot(flatIndex++), arrayType->elemType());
        } else {
            printArray(os, arrayType, dim + 1, flatIndex, indent + "  ");
        }
    }
    os << "]";
}

std::string Array::formatTensorElement(uint64_t flatIndex) const {
    char buf[32];
    std::ostringstream oss;
    switch (elemTypeCode_) {
    case TypeCode::Float32: {
        const double value = getAsDouble(flatIndex);
        if (value == std::floor(value) && std::abs(value) < 1e10) {
            (void)std::snprintf(buf, sizeof(buf), "% 7.0f.", value);
        } else {
            (void)std::snprintf(buf, sizeof(buf), "% 7.4g", value);
        }
        oss << buf;
        break;
    }
    case TypeCode::Int64:
        oss << std::setw(7) << getAsInt64(flatIndex);
        break;
    case TypeCode::Bool:
        oss << (getAsBool(flatIndex) ? "  true" : " false");
        break;
    default:
        oss << "?";
        break;
    }
    return oss.str();
}

void Array::printTensorRecursive(
    std::ostream &os, size_t dim, uint64_t &flatIndex, const std::string &indent) const {
    constexpr int indentStep      = 8;
    constexpr size_t maxLineWidth = 100;
    const std::string innerIndent = indent + std::string(indentStep, ' ');

    if (rank_ == 0) {
        os << formatTensorElement(0);
        return;
    }
    if (dim + 1 == rank_) {
        os << "[";
        size_t currentLineWidth = indent.size() + indentStep;
        for (int64_t i = 0; i < extents_[dim]; ++i) {
            std::string element = formatTensorElement(flatIndex++);
            if (i > 0) {
                const size_t nextWidth = currentLineWidth + 2 + element.size();
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
    os << "[";
    for (int64_t i = 0; i < extents_[dim]; ++i) {
        if (i > 0) {
            os << ",\n" << innerIndent;
        }
        printTensorRecursive(os, dim + 1, flatIndex, innerIndent);
    }
    os << "]";
}

void Array::printTensor(std::ostream &os) const {
    os << "tensor(";
    uint64_t flatIndex = 0;
    printTensorRecursive(os, 0, flatIndex, "");
    os << ", dtype=" << storageName(elemTypeCode_);
    os << ")";
}

void Array::updateRefs(
    const rtdata::Object::RefRelocator &relocate, const type::Type *typeInfo) {
    if (isCompact()) {
        // A compact buffer holds numbers only: no reference to relocate.
        return;
    }
    if (typeInfo == nullptr || typeInfo->code() != TypeCode::Array) {
        return;
    }
    const auto *arrayType       = static_cast<const type::ArrayType *>(typeInfo);
    const type::TypeCode elemCode = arrayType->elemTypeCode();
    const type::Type *elemType    = arrayType->elemType();

    if (fixedArray_ != nullptr) {
        rtdata::Object *newPtr = relocate(
            fixedArray_,
            typeInfo,
            rtdata::RefTraceInfo{
                .owner     = this,
                .ownerType = typeInfo,
                .slotType  = typeInfo,
                .ownerKind = "Array",
                .slotName  = "fixedArray",
                .slotIndex = rtdata::RefTraceInfo::npos,
            });
        fixedArray_ = static_cast<FixedArray *>(newPtr);
        dataPtr_    = reinterpret_cast<std::byte *>(fixedArray_->data());
        return;
    }
    if (!type::isGCTraced(elemCode)) {
        return;
    }
    rtdata::Object **refArr = reinterpret_cast<rtdata::Object **>(inlineData_);
    for (size_t i = 0; i < size_; ++i) {
        if (rtdata::Object *&ref = refArr[i]) {
            ref = relocate(
                ref,
                elemType,
                rtdata::RefTraceInfo{
                    .owner     = this,
                    .ownerType = typeInfo,
                    .slotType  = elemType,
                    .ownerKind = "Array",
                    .slotName  = {},
                    .slotIndex = i,
                });
        }
    }
}