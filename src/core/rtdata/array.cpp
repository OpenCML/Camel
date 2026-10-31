// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The runtime array value: two storage classes, shapes, views and the object protocol.
 * See include/camel/core/rtdata/array.h for the representation rules.
 */

#include "camel/core/rtdata/array.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/complex.h"
#include "camel/core/rtdata/half.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/pure.h"
#include "camel/core/type/text.h"
#include "camel/profile/profile.h"
#include "camel/utils/assert.h"
#include "camel/utils/number_text.h"

#include <cmath>
#include <format>
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

Array *Array::createRankZero(
    camel::core::mm::IAllocator &allocator, const type::ArrayType *arrayType, slot_t value) {
    GUARD(
        arrayType != nullptr,
        "A rank-zero array needs its type, and this call passed none: the one element it holds "
        "would have no type to be read by");
    // No extents at all: the value has no dimension, and `create` gives it the one element an
    // empty product counts (design principles 5.4 -- a rank-zero array is the element itself).
    Array *array = create(allocator, arrayType, std::span<const int64_t>(), false);
    GUARD(
        array->rank() == 0 && array->numel() == 1,
        "A rank-zero array holds one element, and this one does not: the value would be stored at "
        "index 0 of an array that has no such element");
    array->setSlot(0, value, arrayType);
    return array;
}

Array *
Array::view(Array *source, std::span<const int64_t> extents, uint64_t elementOffset) {
    GUARD(source != nullptr, "A view needs a source array, and the call passed none");
    GUARD(source->isCompact(), "Only a compact pure-data array can be viewed, and the source this view names is not one");
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
    GUARD(block != nullptr, "An array wrap needs a block to wrap, and the call passed none");
    GUARD(arrayType != nullptr, "An array wrap needs the element type of what it wraps, and the call passed none");
    GUARD(
        type::arrayStorageFor(arrayType->elemType()) == type::ArrayStorage::Compact,
        "Only a pure-data array can wrap an external block, and this one holds slots: the wrapped "
        "bytes would be read by a layout that does not describe them");
    return createCompact(
        allocator, arrayType->elemType(), extents, block, byteOffset, false, view);
}

slot_t Array::firstDimensionElement(uint64_t index) {
    if (index >= size_) {
        throw std::invalid_argument("The first dimension of an array has no such element");
    }
    if (rank_ <= 1) {
        // Rank 1: the first dimension's elements are the elements themselves.
        return getSlot(index);
    }
    // A higher rank: the row at `index`, which is where the first dimension's stride takes it —
    // the elements after the first extent, as the type states them (firstDimensionElement of
    // ArrayType). Row-major, so a row is a run of `numel / size` elements.
    const uint64_t row = numel_ / size_;
    const uint64_t at  = index * row;
    if (storage_ == Storage::Compact) {
        // The row shares the data: a view of one dimension fewer (arrays are immutable values,
        // and a view is how a reader of the type reads a row).
        return rtdata::toSlot(
            Array::view(this, std::span<const int64_t>(extents_ + 1, rank_ - 1), at));
    }
    // An array of references with more than one dimension: its row is the same run of slots,
    // copied into an array of the row's shape. The copy is the same value, because an array is
    // immutable.
    const std::span<const int64_t> rowExtents(extents_ + 1, rank_ - 1);
    Array *elements = createSlotsRank(*allocator_, rowExtents, static_cast<size_t>(row));
    elements->elemTypeCode_ = elemTypeCode_;
    elements->elemType_     = elemType_;
    // The row's type, for the write barrier of every reference the copy stores: the elements are
    // reachable from the new array, and it is the array of the row's shape the type says. (The
    // element type is interned and lives for the process, so the cast only drops the `const` this
    // value's own description carries.)
    const type::ArrayType *rowType =
        type::ArrayType::create(const_cast<type::Type *>(elemType_), rowExtents);
    for (uint64_t i = 0; i < row; ++i) {
        elements->setSlot(i, getSlot(at + i), rowType);
    }
    return rtdata::toSlot(elements);
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

    // A value with no dimension holds its one element: size is that element's count, not the 0
    // an empty extent would give (design principles 5.4).
    array->size_     = extents.empty() ? numel : static_cast<uint64_t>(extents[0]);
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
    GUARD(elemType != nullptr, "A compact array needs its element type, and the call passed none");
    const type::DataLayout *layout = type::pureDataLayout(elemType);
    GUARD(layout != nullptr, "A compact array needs the layout of its element type, and the type has none");
    // Inline bytes start right after the extent table, which is 8-byte aligned; a layout that
    // needs stricter alignment (a complex number later) must not go inline.
    GUARD(
        layout->alignment <= alignof(slot_t),
        std::format(
            "An inline compact element must not need more than slot alignment, and this one "
            "needs {}: the inline bytes are aligned to a slot",
            layout->alignment));
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
    // A value with no dimension holds its one element (design principles 5.4): size is that
    // element's count, not the 0 an empty extent would give.
    array->size_         = extents.empty() ? numel : static_cast<uint64_t>(extents[0]);
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
        array->noteBlockHeld(block);
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
        array->noteBlockHeld(created);
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
    case TypeCode::Bool: {
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
        // Unreachable: the caller reads a leaf the layout names, and every leaf code has a case
        // above. Reaching it means the layout and this reader disagree, which is a defect of
        // the compiler, so say that rather than abort.
        throw std::runtime_error(std::format(
            "Not a pure-data leaf type: type code {} has no leaf reader",
            static_cast<int>(code)));
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
        // Unreachable: the caller writes a leaf the layout names, and every leaf code has a case
        // above. Reaching it means the layout and this writer disagree, which is a defect of
        // the compiler, so say that rather than abort.
        throw std::runtime_error(std::format(
            "Not a pure-data leaf type: type code {} has no leaf writer",
            static_cast<int>(code)));
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
    GUARD(
        layout != nullptr && layout->aggregate,
        std::format(
            "storing a value of type code {} field by field, but that type has no aggregate layout",
            static_cast<int>(type->code())));
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
        GUARD(fieldLayout != nullptr, "A pure-data field needs the layout of its type, and that type has no layout");
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
    GUARD(
        layout != nullptr && layout->aggregate,
        std::format(
            "reading a value of type code {} field by field, but that type has no aggregate layout",
            static_cast<int>(type->code())));
    const auto fieldType = [type](size_t i) -> const type::Type * {
        return type->code() == TypeCode::Struct
                   ? static_cast<const type::StructType *>(type)->typeAt(i)
                   : static_cast<const type::TupleType *>(type)->typeAt(i);
    };
    bool hasNested = false;
    for (size_t i = 0; i < layout->fieldCount; ++i) {
        const type::DataLayout *fieldLayout = type::pureDataLayout(fieldType(i));
        GUARD(fieldLayout != nullptr, "A pure-data field needs the layout of its type, and that type has no layout");
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
            GUARD(fieldLayout != nullptr, "A pure-data field needs the layout of its type, and that type has no layout");
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
        GUARD(fieldLayout != nullptr, "A pure-data field needs the layout of its type, and that type has no layout");
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
    GUARD(
        flatIndex < numel_,
        std::format(
            "reading element {} of an array that holds {} (rank {}), so the read is past its end",
            flatIndex, numel_, rank_));
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
        // Unreachable: a compact leaf is 1, 2, 4 or 8 bytes wide and a slot is 8, and the eight-
        // byte case returned above; aggregate and complex elements returned before this switch.
        // Reaching it means a layout states a width this reader does not know, i.e. a defect of
        // the compiler, so say that rather than abort.
        throw std::runtime_error(std::format(
            "Unsupported compact element size: {} bytes has no numeric reader", elementSize_));
    }
}

void Array::setSlotSlow(uint64_t flatIndex, slot_t value, const type::ArrayType *arrayType) {
    // Inclusive: an append stores at `numel_` before the count grows (see Array::setSlot). The old
    // exclusive bound aborted every compact append in a Debug build.
    GUARD(
        flatIndex <= numel_,
        std::format(
            "storing element {} of an array that holds {} (rank {}); an append may store at the "
            "end ({}) and nowhere past it",
            flatIndex, numel_, rank_, numel_));
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
        // Unreachable: the same widths the reader knows, and the eight-byte case returned above.
        // Reaching it means a layout states a width this writer does not know, i.e. a defect of
        // the compiler, so say that rather than abort.
        throw std::runtime_error(std::format(
            "Unsupported compact element size: {} bytes has no numeric writer", elementSize_));
    }
}

double Array::getAsDouble(uint64_t flatIndex) const {
    GUARD(
        flatIndex < numel_,
        std::format(
            "reading a number out of element {} of an array that holds {}", flatIndex, numel_));
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
    case TypeCode::BFloat16:
        return static_cast<double>(dataAs<rtdata::BFloat16>()[flatIndex].toFloat());
    case TypeCode::Float16:
        return static_cast<double>(dataAs<rtdata::Float16>()[flatIndex].toFloat());
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

int64_t Array::getAsInt64(uint64_t flatIndex) const {
    GUARD(
        flatIndex < numel_,
        std::format(
            "reading an integer out of element {} of an array that holds {}", flatIndex, numel_));
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
    case TypeCode::BFloat16:
        return static_cast<int64_t>(dataAs<rtdata::BFloat16>()[flatIndex].toFloat());
    case TypeCode::Float16:
        return static_cast<int64_t>(dataAs<rtdata::Float16>()[flatIndex].toFloat());
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

bool Array::getAsBool(uint64_t flatIndex) const {
    GUARD(
        flatIndex < numel_,
        std::format(
            "reading a bool out of element {} of an array that holds {}", flatIndex, numel_));
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
    case TypeCode::BFloat16:
        return dataAs<rtdata::BFloat16>()[flatIndex].toFloat() != 0.0f;
    case TypeCode::Float16:
        return dataAs<rtdata::Float16>()[flatIndex].toFloat() != 0.0f;
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

void Array::setFromDouble(uint64_t flatIndex, double value) {
    GUARD(
        flatIndex < numel_,
        std::format(
            "writing a number to element {} of an array that holds {}", flatIndex, numel_));
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
    case TypeCode::BFloat16:
        dataAs<rtdata::BFloat16>()[flatIndex] = rtdata::BFloat16(value);
        break;
    case TypeCode::Float16:
        dataAs<rtdata::Float16>()[flatIndex] = rtdata::Float16(value);
        break;
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

void Array::setFromInt64(uint64_t flatIndex, int64_t value) {
    GUARD(
        flatIndex < numel_,
        std::format(
            "writing an integer to element {} of an array that holds {}", flatIndex, numel_));
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
    case TypeCode::BFloat16:
        dataAs<rtdata::BFloat16>()[flatIndex] = rtdata::BFloat16(value);
        break;
    case TypeCode::Float16:
        dataAs<rtdata::Float16>()[flatIndex] = rtdata::Float16(value);
        break;
    default:
        throw std::invalid_argument("Unsupported numeric element type");
    }
}

void Array::setFromBool(uint64_t flatIndex, bool value) {
    GUARD(
        flatIndex < numel_,
        std::format(
            "writing a bool to element {} of an array that holds {}", flatIndex, numel_));
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
    case TypeCode::BFloat16:
        dataAs<rtdata::BFloat16>()[flatIndex] = rtdata::BFloat16(value ? 1.0f : 0.0f);
        break;
    case TypeCode::Float16:
        dataAs<rtdata::Float16>()[flatIndex] = rtdata::Float16(value ? 1.0f : 0.0f);
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
    GUARD(isCompact(), "Only a compact array grows this way, and this array holds other arrays instead of pure data");
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
        noteBlockHeld(block);
        return;
    }
    if (block_->unique() && offset_ == 0 && !view_) {
        block_->grow(newBytes);
        dataPtr_  = block_->data();
        capacity_ = newCapacity;
        noteBlockHeld(block_);
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
    noteBlockHeld(block);
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
    GUARD(isCompact(), "Only a compact array adopts a block, and this array holds other arrays instead of pure data");
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
    noteBlockHeld(block);
}

bool Array::writableInPlace() const {
    // One answer for every in-place update, and the one `std::profile` reports: the storage is
    // this value's alone, so a write through it changes no other name (array.h).
    const bool alone = [this] {
        if (view_ || offset_ != 0) {
            return false; // reads someone else's block, or starts inside one
        }
        if (isSlots()) {
            // A slot array's `FixedArray` is its own: the second name gets a copy, so no count
            // has to be kept apart.
            return true;
        }
        if (block_ == nullptr) {
            return true; // inline: this array object is the storage, and the second name copied it
        }
        return block_->unique() && block_->camelOwned();
    }();
    if (camel::profile::enabledInline()) {
        camel::profile::arrayCountAnswer(alone);
    }
    return alone;
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
    noteBlockHeld(block);
}

Array *Array::shareForSecondName(camel::core::mm::IAllocator &allocator) {
    // A write through one name is visible through another only when the two names are the same
    // storage. An array no write can reach -- a view, one shifted from its block's start, an
    // imported block -- hands itself out.
    if (view_ || offset_ != 0) {
        return this;
    }
    if (isSlots()) {
        // A slot array has no block whose references could count the names, so the second name
        // gets a storage of its own: each name is then the only holder of what it writes, and
        // `writableInPlace` answers true for both (design principles 10.5).
        const type::Type *elemType = elemType_;
        if (elemType == nullptr) {
            return this; // not a value a program names: nothing to copy the slots with
        }
        const type::ArrayType *arrayType = type::ArrayType::create(
            const_cast<type::Type *>(elemType), std::span<const int64_t>(extents_, rank_));
        return static_cast<Array *>(clone(allocator, arrayType, false));
    }
    const std::span<const int64_t> extents(extents_, rank_);
    if (block_ == nullptr) {
        // Inline data: two names for one inline storage would be two names for one array, so the
        // second name gets its own copy of the data. It stays inline (the bytes fit the limit),
        // so no block is made and each name is the storage of its own value again.
        Array *second = createCompact(allocator, elemType_, extents, nullptr, 0, false, false);
        if (numel_ > 0) {
            std::memcpy(second->dataPtr_, dataPtr_, byteSize());
        }
        return second;
    }
    if (!block_->camelOwned()) {
        return this; // an imported block is never written in place (design principles 11.3)
    }
    if (!block_->unique()) {
        return this; // several values already hold the block: it is not alone, and stays so
    }
    // The block is this value's alone: the second name is a value over the same block, so the
    // block's references count the names and the next write that needs the only one copies.
    return createCompact(allocator, elemType_, extents, block_, offset_, false, false);
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

Array::Block *Array::heldBlock() const noexcept { return block_; }

void Array::noteBlockHeld(Block *block) {
    if (block != nullptr && allocator_ != nullptr) {
        allocator_->noteBlockHeld(this, block);
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
    // A float array compares by value as well (design principles 7.3): a NaN equals nothing and
    // the two zeros are equal, where a memcmp compares the bit patterns. Every element is read
    // as the number its dtype says, exactly (getAsDouble widens f32, bf16 and f16 without a
    // loss), so this is the rule the scalar `==` of the same type follows.
    const bool ieeeFloat = elemTypeCode_ == TypeCode::Float32 ||
                           elemTypeCode_ == TypeCode::Float64 ||
                           elemTypeCode_ == TypeCode::BFloat16 ||
                           elemTypeCode_ == TypeCode::Float16;
    if (ieeeFloat) {
        for (uint64_t i = 0; i < numel_; ++i) {
            if (!(getAsDouble(i) == other->getAsDouble(i))) {
                return false;
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
    return equalsContent(reinterpret_cast<const Array *>(other), typeInfo, deep);
}

bool Array::equalsContent(
    const Array *rhs, const type::Type *typeInfo, bool deep) const {
    if (!rtdata::isOfSameCls(this, rhs)) {
        return false;
    }
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

    GUARD(
        typeInfo && typeInfo->code() == TypeCode::Array,
        std::format(
            "copying a value whose type code is {}, but the copy reads an ArrayType",
            typeInfo == nullptr ? -1 : static_cast<int>(typeInfo->code())));
    const auto *arrayType            = static_cast<const type::ArrayType *>(typeInfo);
    const type::TypeCode elemTypeCode = arrayType->elemTypeCode();
    // A slot array of any rank keeps its shape: `size_` is the first extent only, so a rank-2
    // value copied over it would come out as its first row (design principles 5.4). Every element
    // is `numel_`, which is `size_` for the rank-1 arrays this has always copied.
    Array *copy = createSlotsRank(allocator, std::span<const int64_t>(extents_, rank_), capacity_);
    copy->elemTypeCode_ = elemTypeCode;
    copy->elemType_     = arrayType->elemType();
    const rtdata::Object *const *srcArr =
        reinterpret_cast<const rtdata::Object *const *>(dataPtr_);

    if (type::isGCTraced(elemTypeCode) && fixedArray_ == nullptr) {
        for (size_t i = 0; i < numel_; ++i) {
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
        std::memcpy(copy->inlineData_, dataPtr_, numel_ * sizeof(slot_t));
        if (type::isGCTraced(elemTypeCode)) {
            for (size_t i = 0; i < numel_; ++i) {
                camel::core::mm::writeBarrier(
                    copy, arrayType, copy->inlineData_[i], arrayType->elemType());
            }
        }
    }
    return copy;
}

void Array::print(std::ostream &os, const type::Type *typeInfo) const {
    if (rank_ == 0) {
        // A value with no dimension is the element it holds: it prints as
        // that value, not as a bracketed list of none.
        rtdata::printSlot(os, getSlot(0), const_cast<type::Type *>(elemType_));
        return;
    }
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
        // The declared element type is what the elements print by, unless the declaration does
        // not fix one (its element is `Void`), and the elements of a value of that
        // type print by the element type the value holds (plan section 5.4). A tensor is an array
        // and prints like one, `[1, 2, 3]` and `[[1, 2], [3, 4]]`, with no `tensor(...)` wrapper.
        const type::Type *elemType = static_cast<const type::ArrayType *>(typeInfo)->elemType();
        if (elemType == nullptr || elemType->code() == TypeCode::Void) {
            elemType = elemType_;
        }
        uint64_t flatIndex = 0;
        printArray(os, elemType, 0, flatIndex);
        return;
    }
    // The declared type does not say this is an array (it may be a scalar the checker left
    // unresolved): print the array the value is, from the
    // element type the value holds.
    uint64_t flatIndex = 0;
    printArray(os, elemType_, 0, flatIndex);
}

void Array::printArray(
    std::ostream &os, const type::Type *elemType, size_t dim, uint64_t &flatIndex) const {
    os << "[";
    const int64_t extent = extents_[dim];
    for (int64_t i = 0; i < extent; ++i) {
        if (i > 0) {
            os << ", ";
        }
        if (dim + 1 == rank_) {
            rtdata::printSlot(os, getSlot(flatIndex++), const_cast<type::Type *>(elemType));
        } else {
            printArray(os, elemType, dim + 1, flatIndex);
        }
    }
    os << "]";
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