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
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/pure.h"
#include "camel/core/type/text.h"
#include "camel/utils/assert.h"
#include "camel/utils/number_text.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
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
    const type::Type *elemType = arrayType != nullptr ? arrayType->elemType() : nullptr;
    const TypeCode elemCode    = elemType != nullptr ? elemType->code() : TypeCode::Void;
    const type::ArrayStorage storage = type::arrayStorageFor(elemType);

    if (storage == type::ArrayStorage::Slots) {
        Array *array = createSlotsRank(allocator, extents, static_cast<size_t>(numel));
        array->elemTypeCode_ = elemCode;
        array->elemType_     = elemType;
        return array;
    }

    return createCompact(allocator, elemType, extents, nullptr, 0, zeroInit, false);
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
    ASSERT(source->isCompact(), "Only a compact pure-data array can be viewed");
    const uint64_t elements = checkedNumel(extents);
    if (elementOffset > source->numel_ || elements > source->numel_ - elementOffset) {
        throw std::invalid_argument("Array view must stay within its source");
    }
    source->ensureBlock();
    Block *block = source->block_;
    const size_t byteOffset =
        source->offset_ + static_cast<size_t>(elementOffset) * source->elementSize_;
    Array *view =
        createCompact(*source->allocator_, source->elemType_, extents, block, byteOffset,
                      false, true);
    return view;
}

Array *Array::wrap(
    camel::core::mm::IAllocator &allocator, const type::ArrayType *arrayType,
    std::span<const int64_t> extents, Block *block, size_t byteOffset, bool view) {
    ASSERT(block != nullptr, "An array wrap needs a block");
    ASSERT(arrayType != nullptr, "An array wrap needs its element type");
    ASSERT(
        type::arrayStorageFor(arrayType->elemType()) == type::ArrayStorage::Compact,
        "Only a pure-data array can wrap an external block");
    return createCompact(
        allocator, arrayType->elemType(), extents, block, byteOffset, false, view);
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
    camel::core::mm::IAllocator &allocator, const type::Type *elemType,
    std::span<const int64_t> extents, Block *block, size_t byteOffset, bool zeroInit, bool view) {
    ASSERT(elemType != nullptr, "A compact array needs its element type");
    const type::DataLayout *layout = type::pureDataLayout(elemType);
    ASSERT(layout != nullptr, "A compact array's element type is pure data");
    // Inline bytes start right after the extent table, which is 8-byte aligned; a layout that
    // needs stricter alignment (a complex number later) must not go inline.
    ASSERT(
        layout->alignment <= alignof(slot_t),
        "An inline compact element must not need more than slot alignment");
    const uint64_t numel  = checkedNumel(extents);
    const size_t elemSize = layout->size;
    const size_t bytes    = static_cast<size_t>(numel) * elemSize;
    const size_t rank     = extents.size();
    const bool inlineData = block == nullptr && bytes <= camel::core::mm::kInlineBlockBytes;

    size_t total = sizeof(Array) + rank * 2 * sizeof(int64_t);
    if (inlineData) {
        total += bytes;
    }
    void *memory = allocator.alloc(total, alignof(Array));
    if (!memory) {
        throw std::bad_alloc();
    }
    Array *array         = new (memory) Array();
    array->allocator_    = &allocator;
    array->storage_      = Storage::Compact;
    array->elemTypeCode_ = elemType->code();
    array->elemType_     = elemType;
    array->elementSize_  = static_cast<uint32_t>(elemSize);
    array->rank_         = static_cast<uint32_t>(rank);
    array->numel_        = numel;
    array->size_         = extents.empty() ? 0 : static_cast<uint64_t>(extents[0]);
    array->view_         = view;
    array->slotShaped_   = layout != nullptr && layout->slotShaped;
    array->extents_      = reinterpret_cast<int64_t *>(array + 1);
    array->strides_      = array->extents_ + rank;
    for (size_t i = 0; i < rank; ++i) {
        array->extents_[i] = extents[i];
    }
    fillStrides(extents, array->strides_);

    if (block != nullptr) {
        array->block_ = block;
        block->retain();
        array->offset_ = byteOffset;
        // A view never grows into the source's storage: any append copies first.
        array->capacity_ = view ? numel : (elemSize == 0 ? 0 : block->bytes() / elemSize);
        array->dataPtr_  = block->data() + byteOffset;
        array->noteFinalizable();
    } else if (inlineData) {
        array->block_    = nullptr;
        array->capacity_  = numel;
        array->dataPtr_   = reinterpret_cast<std::byte *>(array->strides_ + rank);
        if (zeroInit && bytes > 0) {
            std::memset(array->dataPtr_, 0, bytes);
        }
    } else {
        Block *created = camel::core::mm::Block::create(bytes);
        array->block_  = created;
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
    if (block_ != nullptr && allocator_ != nullptr) {
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
    if (block_ != nullptr) {
        dataPtr_ = block_->data() + offset_;
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

namespace {

using camel::core::type::TypeCode;

/// Reads one pure-data leaf from its compact bytes. The slot carries the same low bytes that
/// rtdata::toSlot gives the value, so this is the inverse of writeLeaf.
slot_t readLeaf(const std::byte *at, TypeCode code, camel::core::mm::IAllocator &allocator) {
    switch (code) {
    case TypeCode::Int32: {
        int32_t value = 0;
        std::memcpy(&value, at, sizeof(value));
        return rtdata::toSlot(value);
    }
    case TypeCode::Int64: {
        int64_t value = 0;
        std::memcpy(&value, at, sizeof(value));
        return rtdata::toSlot(value);
    }
    case TypeCode::Float32: {
        float value = 0.0f;
        std::memcpy(&value, at, sizeof(value));
        return rtdata::toSlot(value);
    }
    case TypeCode::Float64: {
        double value = 0.0;
        std::memcpy(&value, at, sizeof(value));
        return rtdata::toSlot(value);
    }
    case TypeCode::Bool:
    case TypeCode::Byte: {
        const uint8_t value = *reinterpret_cast<const uint8_t *>(at);
        return rtdata::toSlot(value);
    }
    case TypeCode::Complex64: {
        float real = 0.0f, imaginary = 0.0f;
        std::memcpy(&real, at, sizeof(real));
        std::memcpy(&imaginary, at + sizeof(real), sizeof(imaginary));
        return rtdata::toSlot(rtdata::Complex::create(real, imaginary, code, allocator));
    }
    case TypeCode::Complex128: {
        double real = 0.0, imaginary = 0.0;
        std::memcpy(&real, at, sizeof(real));
        std::memcpy(&imaginary, at + sizeof(real), sizeof(imaginary));
        return rtdata::toSlot(rtdata::Complex::create(real, imaginary, code, allocator));
    }
    default:
        ASSERT(false, "Not a pure-data leaf type");
        return NullSlot;
    }
}

/// Writes one pure-data leaf into its compact bytes. A Bool is normalized to 0/1 so that two
/// equal values have equal bytes.
void writeLeaf(std::byte *at, TypeCode code, slot_t slot) {
    switch (code) {
    case TypeCode::Int32: {
        const int32_t value = rtdata::fromSlot<int32_t>(slot);
        std::memcpy(at, &value, sizeof(value));
        break;
    }
    case TypeCode::Int64: {
        const int64_t value = rtdata::fromSlot<int64_t>(slot);
        std::memcpy(at, &value, sizeof(value));
        break;
    }
    case TypeCode::Float32: {
        const float value = rtdata::fromSlot<float>(slot);
        std::memcpy(at, &value, sizeof(value));
        break;
    }
    case TypeCode::Float64: {
        const double value = rtdata::fromSlot<double>(slot);
        std::memcpy(at, &value, sizeof(value));
        break;
    }
    case TypeCode::Bool: {
        const uint8_t value = rtdata::fromSlot<uint8_t>(slot) != 0 ? 1 : 0;
        std::memcpy(at, &value, sizeof(value));
        break;
    }
    case TypeCode::Byte: {
        const uint8_t value = rtdata::fromSlot<uint8_t>(slot);
        std::memcpy(at, &value, sizeof(value));
        break;
    }
    case TypeCode::Complex64: {
        float real = 0.0f, imaginary = 0.0f;
        if (slot != NullSlot) {
            const auto *value = rtdata::fromSlot<const rtdata::Complex *>(slot);
            real              = value->realF();
            imaginary         = value->imaginaryF();
        }
        std::memcpy(at, &real, sizeof(real));
        std::memcpy(at + sizeof(real), &imaginary, sizeof(imaginary));
        break;
    }
    case TypeCode::Complex128: {
        double real = 0.0, imaginary = 0.0;
        if (slot != NullSlot) {
            const auto *value = rtdata::fromSlot<const rtdata::Complex *>(slot);
            real              = value->real();
            imaginary         = value->imaginary();
        }
        std::memcpy(at, &real, sizeof(real));
        std::memcpy(at + sizeof(real), &imaginary, sizeof(imaginary));
        break;
    }
    default:
        ASSERT(false, "Not a pure-data leaf type");
        break;
    }
}

/// Stores a materialized Struct/Tuple into the bytes of its compact element. Same field order
/// and layout as materializeAggregate, so an element round-trips. The source may itself be a
/// view (a map callback returning its element); reading it through get() needs no materialise.
void storeAggregate(std::byte *bytes, const type::Type *type, slot_t value) {
    if (value == NullSlot) {
        return; // all-zero bytes describe the zero value
    }
    const type::DataLayout *layout = type::pureDataLayout(type);
    ASSERT(layout != nullptr && layout->aggregate, "storeAggregate expects an aggregate type");
    const auto fieldType = [type](size_t i) -> const type::Type * {
        return type->code() == TypeCode::Struct
                   ? static_cast<const type::StructType *>(type)->typeAt(i)
                   : static_cast<const type::TupleType *>(type)->typeAt(i);
    };
    // A slot-shaped aggregate (every field an 8-byte leaf, no padding) is byte-identical to a
    // real Struct/Tuple's slot data, so a map that builds tuples can be copied in one go.
    if (layout->slotShaped) {
        if (type->code() == TypeCode::Struct) {
            auto *source = rtdata::fromSlot<::Struct *>(value);
            if (!source->isView()) {
                std::memcpy(bytes, source->data(), layout->size);
                return;
            }
        } else {
            auto *source = rtdata::fromSlot<::Tuple *>(value);
            if (!source->isView()) {
                std::memcpy(bytes, source->data(), layout->size);
                return;
            }
        }
    }
    const auto fieldValue = [type, value](size_t i) -> slot_t {
        if (type->code() == TypeCode::Struct) {
            auto *source = rtdata::fromSlot<::Struct *>(value);
            return source->isView() ? source->get<slot_t>(i) : source->data()[i];
        }
        auto *source = rtdata::fromSlot<::Tuple *>(value);
        return source->isView() ? source->get<slot_t>(i) : source->data()[i];
    };
    for (size_t i = 0; i < layout->fieldCount; ++i) {
        const type::Type *field      = fieldType(i);
        const type::DataLayout *fieldLayout = type::pureDataLayout(field);
        ASSERT(fieldLayout != nullptr, "A pure-data field has a layout");
        std::byte *fieldBytes = bytes + layout->fieldOffsets[i];
        if (fieldLayout->aggregate) {
            storeAggregate(fieldBytes, field, fieldValue(i));
        } else {
            writeLeaf(fieldBytes, fieldLayout->scalar, fieldValue(i));
        }
    }
}



} // namespace

namespace camel::core::rtdata {

rtdata::Object *
materializeAggregate(const std::byte *bytes, const type::Type *type, camel::core::mm::IAllocator &allocator) {
    const type::DataLayout *layout = type::pureDataLayout(type);
    ASSERT(layout != nullptr && layout->aggregate, "materializeAggregate expects an aggregate");
    const auto fieldType = [type](size_t i) -> const type::Type * {
        return type->code() == TypeCode::Struct
                   ? static_cast<const type::StructType *>(type)->typeAt(i)
                   : static_cast<const type::TupleType *>(type)->typeAt(i);
    };
    bool hasNested = false;
    for (size_t i = 0; i < layout->fieldCount; ++i) {
        const type::DataLayout *fieldLayout = type::pureDataLayout(fieldType(i));
        ASSERT(fieldLayout != nullptr, "A pure-data field has a layout");
        // A complex leaf allocates its box while the parent exists: that is a nested
        // allocation too, so the parent must be rooted across it.
        hasNested = hasNested || fieldLayout->aggregate ||
                    camel::core::type::isComplex(fieldLayout->scalar);
    }

    // A nested field is materialized by an allocation after the parent exists. Root the parent
    // across that allocation: the collector may move it, and the raw pointer would go stale.
    camel::core::mm::GenerationalAllocatorWithGC *gc =
        hasNested ? dynamic_cast<camel::core::mm::GenerationalAllocatorWithGC *>(&allocator)
                  : nullptr;
    std::unique_ptr<camel::core::mm::RootHandle> root;

    if (type->code() == TypeCode::Struct) {
        auto *object = ::Struct::create(layout->fieldCount, allocator);
        if (gc) {
            root = std::make_unique<camel::core::mm::RootHandle>(
                *gc, object, type, "materializeAggregate");
        }
        for (size_t i = 0; i < layout->fieldCount; ++i) {
            const type::Type *field             = fieldType(i);
            const type::DataLayout *fieldLayout = type::pureDataLayout(field);
            ASSERT(fieldLayout != nullptr, "A pure-data field has a layout");
            const std::byte *fieldBytes = bytes + layout->fieldOffsets[i];
            const slot_t value = fieldLayout->aggregate
                                     ? rtdata::toSlot(static_cast<rtdata::Object *>(
                                           rtdata::materializeAggregate(fieldBytes, field, allocator)))
                                     : readLeaf(fieldBytes, fieldLayout->scalar, allocator);
            if (root) {
                object = root->getAs<::Struct>();
            }
            object->set<slot_t>(i, value);
        }
        return object;
    }

    auto *object = ::Tuple::create(layout->fieldCount, allocator);
    if (gc) {
        root = std::make_unique<camel::core::mm::RootHandle>(
            *gc, object, type, "materializeAggregate");
    }
    for (size_t i = 0; i < layout->fieldCount; ++i) {
        const type::Type *field             = fieldType(i);
        const type::DataLayout *fieldLayout = type::pureDataLayout(field);
        ASSERT(fieldLayout != nullptr, "A pure-data field has a layout");
        const std::byte *fieldBytes = bytes + layout->fieldOffsets[i];
        const slot_t value          = fieldLayout->aggregate
                                          ? rtdata::toSlot(static_cast<rtdata::Object *>(
                                                rtdata::materializeAggregate(fieldBytes, field, allocator)))
                                          : readLeaf(fieldBytes, fieldLayout->scalar, allocator);
        if (root) {
            object = root->getAs<::Tuple>();
        }
        object->set<slot_t>(i, value);
    }
    return object;
}

} // namespace camel::core::rtdata


slot_t Array::getSlotSlow(uint64_t flatIndex) const {
    ASSERT(flatIndex < numel_, "Array flat index out of range");
    if (isSlots()) {
        return reinterpret_cast<const slot_t *>(dataPtr_)[flatIndex];
    }
    const std::byte *at = dataPtr_ + flatIndex * elementSize_;
    if (isAggregateElement()) {
        // Copy the element out before allocating: materializing a Struct/Tuple allocates, and
        // that may move this array (and its data) if it lives in the young generation.
        std::vector<std::byte> copy(elementSize_);
        if (elementSize_ > 0) {
            std::memcpy(copy.data(), at, elementSize_);
        }
        return rtdata::toSlot(
            static_cast<rtdata::Object *>(rtdata::materializeAggregate(copy.data(), elemType_, *allocator_)));
    }
    if (type::isComplex(elemTypeCode_)) {
        return rtdata::readPureComplex(at, elemTypeCode_);
    }
    if (elementSize_ == sizeof(slot_t)) {
        return reinterpret_cast<const slot_t *>(dataPtr_)[flatIndex];
    }
    switch (elementSize_) {
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

void Array::setSlotSlow(uint64_t flatIndex, slot_t value, const type::ArrayType *arrayType) {
    // Inclusive: an append stores at `numel_` before the count grows (see Array::setSlot). The old
    // exclusive bound aborted every compact append in a Debug build (H30).
    ASSERT(flatIndex <= numel_, "Array flat index out of range");
    if (isSlots()) {
        CAMEL_ASSERT_NOT_VIEW(value, elemType_, "an array element");
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
    if (isAggregateElement()) {
        if (slotShaped_ && value != NullSlot) {
            const slot_t *slots = nullptr;
            if (elemType_->code() == TypeCode::Struct) {
                auto *source = rtdata::fromSlot<::Struct *>(value);
                if (!source->isView()) {
                    slots = source->data();
                }
            } else {
                auto *source = rtdata::fromSlot<::Tuple *>(value);
                if (!source->isView()) {
                    slots = source->data();
                }
            }
            if (slots != nullptr) {
                std::memcpy(at, slots, elementSize_);
                return;
            }
        }
        if (elementSize_ > 0) {
            std::memset(at, 0, elementSize_);
        }
        storeAggregate(at, elemType_, value);
        return;
    }
    if (type::isComplex(elemTypeCode_)) {
        writeLeaf(at, elemTypeCode_, value);
        return;
    }
    if (elementSize_ == sizeof(slot_t)) {
        reinterpret_cast<slot_t *>(dataPtr_)[flatIndex] = value;
        return;
    }
    switch (elementSize_) {
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
    if (block_ == nullptr) {
        // Inline data: move it into a block, which is what makes the array growable and gives
        // views a stable address.
        Block *block = camel::core::mm::Block::create(newBytes);
        const size_t used = byteSize();
        if (used > 0) {
            std::memcpy(block->data(), dataPtr_, used);
        }
        block_   = block;
        dataPtr_  = block->data();
        offset_   = 0;
        capacity_ = newCapacity;
        noteFinalizable();
        return;
    }
    if (block_->unique() && offset_ == 0 && !view_) {
        block_->grow(newBytes);
        dataPtr_  = block_->data();
        capacity_ = newCapacity;
        return;
    }
    // Shared (a view, or another array): copy to a new block and leave the old one to its
    // other holders.
    Block *block = camel::core::mm::Block::create(newBytes);
    const size_t used = byteSize();
    if (used > 0) {
        std::memcpy(block->data(), dataPtr_, used);
    }
    Block *old = block_;
    block_     = block;
    dataPtr_    = block->data();
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

Array::Block *Array::reusableBlock(size_t newBytes) {
    if (isCompact() && block_ != nullptr && block_->unique() && offset_ == 0 && !view_ &&
        block_->bytes() >= newBytes) {
        return block_;
    }
    Block *block = camel::core::mm::Block::create(newBytes);
    const size_t used = std::min(byteSize(), newBytes);
    if (used > 0 && dataPtr_ != nullptr) {
        std::memcpy(block->data(), dataPtr_, used);
    }
    return block;
}

void Array::adoptBlock(Block *block, size_t newBytes) {
    if (block == nullptr || block == block_) {
        return;
    }
    ASSERT(isCompact(), "Only a compact array adopts a block");
    Block *old = block_;
    block_     = block;
    dataPtr_    = block->data();
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
    if (block_ == nullptr) {
        return true; // inline: this array object is the storage, so nothing else can see it
    }
    return block_->unique() && block_->camelOwned() && offset_ == 0 && !view_;
}

void Array::ensureBlock() {
    if (!isCompact() || block_ != nullptr) {
        return;
    }
    const size_t bytes = byteSize();
    Block *block     = camel::core::mm::Block::create(bytes);
    if (bytes > 0) {
        std::memcpy(block->data(), dataPtr_, bytes);
    }
    block_   = block;
    dataPtr_  = block->data();
    offset_   = 0;
    capacity_ = numel_;
    noteFinalizable();
}

void Array::releaseBlock() noexcept {
    if (block_ != nullptr) {
        Block *block = block_;
        block_        = nullptr;
        block->release();
    }
}

void Array::finalize() noexcept {
    if (block_ != nullptr) {
        Block *block = block_;
        block_        = nullptr;
        block->releaseFromFinalizer();
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
    if (type::isComplex(elemTypeCode_)) {
        // Value equality, not byte equality: -0 == 0 and NaN != NaN are the complex number's
        // equality (the same rules as the scalar == operator), while a memcmp would compare
        // the bit patterns.
        for (uint64_t i = 0; i < numel_; ++i) {
            const std::byte *a = dataPtr_ + i * elementSize_;
            const std::byte *b = other->dataPtr_ + i * elementSize_;
            if (elemTypeCode_ == TypeCode::Complex64) {
                float ar = 0.0f, ai = 0.0f, br = 0.0f, bi = 0.0f;
                std::memcpy(&ar, a, sizeof(ar));
                std::memcpy(&ai, a + sizeof(ar), sizeof(ai));
                std::memcpy(&br, b, sizeof(br));
                std::memcpy(&bi, b + sizeof(br), sizeof(bi));
                if (!(ar == br && ai == bi)) {
                    return false;
                }
            } else {
                double ar = 0.0, ai = 0.0, br = 0.0, bi = 0.0;
                std::memcpy(&ar, a, sizeof(ar));
                std::memcpy(&ai, a + sizeof(ar), sizeof(ai));
                std::memcpy(&br, b, sizeof(br));
                std::memcpy(&bi, b + sizeof(br), sizeof(bi));
                if (!(ar == br && ai == bi)) {
                    return false;
                }
            }
        }
        return true;
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
        // A compact aggregate is copied through a snapshot: creating the destination can move
        // this array (and its data) at a safepoint, and the raw data pointer would go stale.
        if (isAggregateElement()) {
            const size_t byteCount = byteSize();
            std::vector<std::byte> bytes(byteCount);
            if (byteCount > 0) {
                std::memcpy(bytes.data(), dataPtr_, byteCount);
            }
            std::vector<int64_t> extents(extents_, extents_ + rank_);
            Array *copy = createCompact(
                allocator, elemType_, std::span<const int64_t>(extents.data(), extents.size()), nullptr, 0, false,
                false);
            if (byteCount > 0) {
                std::memcpy(copy->dataPtr_, bytes.data(), byteCount);
            }
            return copy;
        }
        Array *copy = createCompact(
            allocator, elemType_, std::span<const int64_t>(extents_, rank_), nullptr, 0, false,
            false);
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
    copy->elemType_     = arrayType->elemType();
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
        // A compact aggregate stores its elements as bytes, and printing one materializes a
        // Struct/Tuple, which allocates and may move this array. Copy the bytes and the shape
        // first, then print only from the copy.
        if (isCompact() && isAggregateElement()) {
            std::vector<std::byte> bytes(byteSize());
            if (byteSize() > 0) {
                std::memcpy(bytes.data(), dataPtr_, byteSize());
            }
            std::vector<int64_t> extents(extents_, extents_ + rank_);
            const size_t elementSize = elementSize_;
            const size_t rank        = rank_;
            const type::Type *elemType = elemType_;
            camel::core::mm::IAllocator *allocator = allocator_;
            uint64_t flatIndex       = 0;
            const std::function<void(size_t)> printDim = [&](size_t dim) {
                os << "[";
                for (int64_t i = 0; i < extents[dim]; ++i) {
                    if (i > 0) {
                        os << ", ";
                    }
                    if (dim + 1 == rank) {
                        const slot_t value = rtdata::toSlot(static_cast<rtdata::Object *>(
                            rtdata::materializeAggregate(
                                bytes.data() + flatIndex++ * elementSize, elemType,
                                *allocator)));
                        rtdata::printSlot(os, value, const_cast<type::Type *>(elemType));
                    } else {
                        printDim(dim + 1);
                    }
                }
                os << "]";
            };
            printDim(0);
            return;
        }
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
        // A compact block holds pure data only: no reference to relocate.
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