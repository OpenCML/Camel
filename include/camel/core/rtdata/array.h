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
 * Author: Zhenjie Wei
 * Created: Nov. 07, 2025
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The runtime array value.
 *
 * An array is an element type plus a shape, and two storage classes (design:
 * docs/design/arrays-memory/README.cn.md 3.1, 3.2):
 *
 * - Slots: one 8-byte slot per element, able to hold a reference. This is the representation
 *   every array had before native arrays; it stays for non-numeric element types.
 * - Compact: a contiguous row-major run of numbers. It is a Buffer (core/array/buffer.h) when
 *   it is larger than kInlineArrayBytes or when a view or an export needs a stable address;
 *   otherwise its bytes are inline in the GC object and move with it (no buffer, no atomic
 *   count, no finalizer: small logic programs do not pay for the new memory management).
 *
 * The decision is made once, by type::arrayStorageFor: a numeric element type is compact and
 * anything else is slots. "A compact buffer holds numbers only" is the leaf rule that makes
 * reference counting safe and zero-copy exchange possible.
 *
 * An inline array is promoted to a Buffer when a view is made of it or when it is exported
 * (DLPack): both hand a pointer to code that may outlive the array, and the GC object may
 * move. Promotion copies once, after which the data address is stable.
 *
 * Arrays are immutable values (design principles 5.4): "modifying" an operation returns a new
 * value, except that an operation may update in place when nothing else can see the old value
 * (design principles 10.5). `reusableBuffer` is the hook for that: it gives the operation the
 * array's own buffer when the count is 1 and the shape matches, and a fresh one otherwise.
 */

#pragma once

#include "base.h"
#include "camel/core/array/buffer.h"
#include "camel/core/type/composite/array.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <ostream>
#include <span>
#include <string>
#include <vector>

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

// FixedArray: fixed-size slot array with object body and data area in one contiguous block.
// Only the slot representation uses it (the backing store of an Array past its inline slots).
class FixedArray : public rtdata::Object {
  public:
    // Direct construction is forbidden.
    FixedArray(const FixedArray &)            = delete;
    FixedArray &operator=(const FixedArray &) = delete;

    // Static creation method.
    static FixedArray *create(size_t size, camel::core::mm::IAllocator &allocator) {
        size_t headerSize = sizeof(FixedArray);
        size_t dataSize   = size * sizeof(slot_t);
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(FixedArray));
        if (!memory)
            throw std::bad_alloc();

        FixedArray *array = new (memory) FixedArray(size);
        std::fill(array->data_, array->data_ + size, NullSlot);
        return array;
    }

    size_t size() const { return size_; }
    slot_t *data() { return data_; }
    const slot_t *data() const { return data_; }

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return fromSlot<T>(data_[index]);
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, "Index out of range");
        data_[index] = toSlot(value);
    }

    template <typename T> void set(size_t index, T value, const type::ArrayType *arrayType) {
        ASSERT(index < size_, "Index out of range");
        ASSERT(arrayType != nullptr, "ArrayType is required for a typed FixedArray write.");
        const slot_t slot = rtdata::toSlot(value);
        camel::core::mm::writeBarrier(this, arrayType, slot, arrayType->elemType());
        data_[index] = slot;
    }

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        if (this == other)
            return true;
        if (!isOfSameCls(this, other))
            return false;

        ASSERT(type && type->code() == type::TypeCode::Array, "Type must be ArrayType");
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::TypeCode elemTypeCode      = arrayType->elemTypeCode();

        const FixedArray *otherArray = reinterpret_cast<const FixedArray *>(other);
        if (size_ != otherArray->size_)
            return false;

        const rtdata::Object *const *arrA = reinterpret_cast<const rtdata::Object *const *>(data_);
        const rtdata::Object *const *arrB =
            reinterpret_cast<const rtdata::Object *const *>(otherArray->data_);

        if (type::isGCTraced(elemTypeCode)) {
            if (deep) {
                // Deep compare reference objects.
                const type::Type *elemType = arrayType->elemType();
                for (size_t i = 0; i < size_; ++i) {
                    const rtdata::Object *refA = arrA[i];
                    const rtdata::Object *refB = arrB[i];
                    if (refA == refB)
                        continue;
                    if (!refA->equals(refB, elemType, true))
                        return false;
                }
                return true;
            } else {
                // Shallow compare reference pointer addresses.
                return std::memcmp(arrA, arrB, size_ * sizeof(rtdata::Object *)) == 0;
            }
        } else {
            return std::memcmp(arrA, arrB, size_ * sizeof(slot_t)) == 0;
        }
    }

    virtual rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Array, "Type must be ArrayType");
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::TypeCode elemTypeCode      = arrayType->elemTypeCode();

        FixedArray *newArray = FixedArray::create(size_, allocator);
        const rtdata::Object *const *srcArr =
            reinterpret_cast<const rtdata::Object *const *>(data_);
        rtdata::Object **dstArr = reinterpret_cast<rtdata::Object **>(newArray->data_);

        if (type::isGCTraced(elemTypeCode)) {
            for (size_t i = 0; i < size_; ++i) {
                const rtdata::Object *oriRef = srcArr[i];
                if (oriRef) {
                    if (deep) {
                        // Deep copy: recursively clone referenced objects.
                        rtdata::Object *clonedRef =
                            oriRef->clone(allocator, arrayType->elemType(), true);
                        dstArr[i] = clonedRef;
                        camel::core::mm::writeBarrier(
                            newArray,
                            arrayType,
                            clonedRef,
                            arrayType->elemType());
                    } else {
                        // Shallow copy: copy the reference directly.
                        dstArr[i] = const_cast<rtdata::Object *>(oriRef);
                        camel::core::mm::writeBarrier(
                            newArray,
                            arrayType,
                            dstArr[i],
                            arrayType->elemType());
                    }
                } else {
                    dstArr[i] = rtdata::NullRef;
                }
            }
        } else {
            // Non-reference types: copy data directly.
            std::memcpy(dstArr, srcArr, size_ * sizeof(slot_t));
        }

        return reinterpret_cast<rtdata::Object *>(newArray);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override {
        ASSERT(type && type->code() == type::TypeCode::Array, "Type must be ArrayType");
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::Type *elemType             = arrayType->elemType();

        os << "[";
        for (size_t i = 0; i < size_; ++i) {
            if (i > 0)
                os << ", ";
            rtdata::printSlot(os, data_[i], elemType);
        }
        os << "]";
    }

    virtual void onMoved() override {
        // data_ is a flexible array member: its address is an offset from `this`, so it needs
        // no update when the object moves.
    }

    virtual void
    updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override {
        if (!type || type->code() != type::TypeCode::Array)
            return;
        const type::ArrayType *arrayType = static_cast<const type::ArrayType *>(type);
        type::TypeCode elemTypeCode      = arrayType->elemTypeCode();

        if (!type::isGCTraced(elemTypeCode))
            return;

        rtdata::Object **refArr = reinterpret_cast<rtdata::Object **>(data_);
        for (size_t i = 0; i < size_; ++i) {
            if (rtdata::Object *&ref = refArr[i]) {
                type::Type *elemType = arrayType->elemType();
                ref                  = relocate(
                    ref,
                    elemType,
                    rtdata::RefTraceInfo{
                        .owner     = this,
                        .ownerType = type,
                        .slotType  = elemType,
                        .ownerKind = "FixedArray",
                        .slotName  = {},
                        .slotIndex = i,
                    });
            }
        }
    }

  private:
    explicit FixedArray(size_t size) : size_(size) {}

    size_t size_;
    // Flexible array member: must be last.
    slot_t data_[];
};

class Array : public rtdata::Object {
  public:
    using Buffer = camel::core::array::Buffer;
    /// The storage class of this value (type::arrayStorageFor decides it at creation).
    enum class Storage : uint8_t { Slots, Compact };

    Array(const Array &)            = delete;
    Array &operator=(const Array &) = delete;

    // ---- Creation -------------------------------------------------------------------------

    /// A slot array with room for `initSize` elements. The factory for arrays whose element
    /// type is not numeric, or is not known yet (array literals, argument lists).
    static Array *create(camel::core::mm::IAllocator &allocator, size_t initSize = 0);

    /// An array of `arrayType`'s element type and `extents`. Storage follows the element type
    /// (type::arrayStorageFor). `zeroInit` clears compact data; slot arrays are null-filled.
    static Array *create(
        camel::core::mm::IAllocator &allocator, const type::ArrayType *arrayType,
        std::span<const int64_t> extents, bool zeroInit = false);

    /// The rank-1 convenience.
    static Array *create(
        camel::core::mm::IAllocator &allocator, const type::ArrayType *arrayType, size_t size,
        bool zeroInit = false);

    /// A view of `source`'s compact data: `extents` elements starting at `elementOffset`.
    /// The view shares the buffer; an inline source is promoted (copied once) first. Only
    /// compact arrays can be viewed.
    static Array *
    view(Array *source, std::span<const int64_t> extents, uint64_t elementOffset = 0);

    /// An array over `buffer`'s bytes (DLPack import, external memory). The array retains the
    /// buffer; `byteOffset` is in bytes and `view` marks the storage as not owned for growth.
    static Array *wrap(
        camel::core::mm::IAllocator &allocator, const type::ArrayType *arrayType,
        std::span<const int64_t> extents, Buffer *buffer, size_t byteOffset = 0,
        bool view = false);

    // ---- Shape ----------------------------------------------------------------------------

    Storage storage() const { return storage_; }
    bool isCompact() const { return storage_ == Storage::Compact; }
    bool isSlots() const { return storage_ == Storage::Slots; }
    /// True when this value reads another array's buffer rather than owning the storage.
    bool isView() const { return view_; }

    /// Whether this array may be changed in place: an inline compact array is its own storage,
    /// a buffer-backed one needs the only reference to a Camel-owned buffer at offset 0 and
    /// must not be a view. An imported (external) buffer is never writable (design principles
    /// 11.3: another framework must not see the change).
    bool canWriteInPlace() const;

    size_t size() const { return static_cast<size_t>(size_); }
    uint64_t numel() const { return numel_; }
    size_t rank() const { return rank_; }
    int64_t dim(size_t index) const {
        ASSERT(index < rank_, "Array dimension out of range");
        return extents_[index];
    }
    /// The extents as the runtime stores them. For a slot array this is the one extent.
    const int64_t *shape() const { return extents_; }
    std::span<const int64_t> shapeSpan() const { return {extents_, rank_}; }
    std::vector<int64_t> shapeVector() const {
        return std::vector<int64_t>(extents_, extents_ + rank_);
    }
    int64_t step(size_t index) const {
        ASSERT(index < rank_, "Array dimension out of range");
        return strides_[index];
    }
    bool sameShape(const Array *other) const;
    /// Bytes of element data (not the header, not the extent table).
    size_t byteSize() const { return static_cast<size_t>(numel_ * elementSize_); }

    /// The element type code this value stores (Void for a slot array created without one).
    camel::core::type::TypeCode elemTypeCode() const { return elemTypeCode_; }
    /// The name `tensor` uses for the element type.
    camel::core::type::TypeCode dtype() const { return elemTypeCode_; }
    size_t elementSize() const { return elementSize_; }

    // ---- Storage access ------------------------------------------------------------------

    /// Slot data. Only a slot array and an 8-byte compact element type have this layout; use
    /// dataAs<T>() or the get/set accessors otherwise.
    slot_t *data() {
        ASSERT(isSlots() || elementSize_ == sizeof(slot_t), "Use dataAs<T>() for this element type");
        return reinterpret_cast<slot_t *>(dataPtr_);
    }
    const slot_t *data() const {
        ASSERT(isSlots() || elementSize_ == sizeof(slot_t), "Use dataAs<T>() for this element type");
        return reinterpret_cast<const slot_t *>(dataPtr_);
    }
    std::byte *rawData() { return dataPtr_; }
    const std::byte *rawData() const { return dataPtr_; }
    std::byte *elementPtr(uint64_t flatIndex) {
        ASSERT(flatIndex < numel_, "Array flat index out of range");
        return dataPtr_ + flatIndex * elementSize_;
    }
    const std::byte *elementPtr(uint64_t flatIndex) const {
        ASSERT(flatIndex < numel_, "Array flat index out of range");
        return dataPtr_ + flatIndex * elementSize_;
    }

    template <typename T> T *dataAs() {
        ASSERT(isCompact(), "dataAs<T>() requires a compact numeric array");
        return reinterpret_cast<T *>(dataPtr_);
    }
    template <typename T> const T *dataAs() const {
        ASSERT(isCompact(), "dataAs<T>() requires a compact numeric array");
        return reinterpret_cast<const T *>(dataPtr_);
    }

    /// One element as a slot. For a compact array the slot carries the element's own bytes
    /// (an f32 in the low 4 bytes, a bool in the low byte), which is what toSlot/fromSlot give.
    slot_t getSlot(uint64_t flatIndex) const;
    /// Stores one element from a slot. `arrayType` supplies the element type for the reference
    /// write barrier (slot arrays only; a compact write never needs one) and may be null for a
    /// compact array, whose own element type is enough.
    void setSlot(uint64_t flatIndex, slot_t value, const type::ArrayType *arrayType = nullptr);

    template <typename T> T get(size_t index) const {
        ASSERT(index < size_, "Index out of range");
        return rtdata::fromSlot<T>(getSlot(index));
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size_, "Index out of range");
        setSlot(index, rtdata::toSlot(value), nullptr);
    }

    template <typename T> void set(size_t index, T value, const type::ArrayType *arrayType) {
        ASSERT(index < size_, "Index out of range");
        setSlot(index, rtdata::toSlot(value), arrayType);
    }

    // ---- Element access used by tensor kernels (moved from TensorObject) -----------------

    double getAsDouble(uint64_t flatIndex) const;
    int64_t getAsInt64(uint64_t flatIndex) const;
    bool getAsBool(uint64_t flatIndex) const;
    void setFromDouble(uint64_t flatIndex, double value);
    void setFromInt64(uint64_t flatIndex, int64_t value);
    void setFromBool(uint64_t flatIndex, bool value);

    // ---- Size and growth (slot arrays, and compact arrays whose buffer can grow) ----------

    size_t capacity() const { return static_cast<size_t>(capacity_); }
    void reserve(size_t newCapacity);
    void reserve(size_t newCapacity, const type::ArrayType *arrayType);
    void resize(size_t newSize);
    void resize(size_t newSize, const type::ArrayType *arrayType);

    void appendSlot(slot_t value, const type::ArrayType *arrayType);
    template <typename T> void append(const T value) {
        appendSlot(rtdata::toSlot(value), nullptr);
    }
    template <typename T> void append(const T value, const type::ArrayType *arrayType) {
        ASSERT(arrayType != nullptr, "ArrayType is required for a typed Array append.");
        appendSlot(rtdata::toSlot(value), arrayType);
    }

    void pop() {
        ASSERT(size_ > 0, "Cannot pop from empty array");
        --size_;
        --numel_;
        if (isSlots()) {
            extents_[0] = static_cast<int64_t>(size_);
        }
        // Capacity is kept; no shrink.
    }
    void clear() {
        size_  = 0;
        numel_ = 0;
        if (isSlots()) {
            extents_[0] = 0;
        }
    }
    void shrinkToFit();
    void shrinkToFit(const type::ArrayType *arrayType);

    // ---- In-place hook -------------------------------------------------------------------

    /// A buffer the caller may write and adopt as this array's storage: this array's own
    /// buffer when it is compact, unique, and already the right size; a fresh buffer (copied
    /// from this array) otherwise. This is the hook for the in-place guarantee (design
    /// principles 10.5): an operation that returns "a new array" writes into the returned
    /// buffer. `newBytes` is the required data length.
    Buffer *reusableBuffer(size_t newBytes);
    /// Makes this array buffer-backed and (re)points its data at `buffer`, releasing the old
    /// one. Used by in-place operations after a reusableBuffer() call.
    void adoptBuffer(Buffer *buffer, size_t newBytes);
    /// The array's current buffer, or null for a slot array / an inline compact array.
    Buffer *buffer() const { return buffer_; }
    /// Copies inline data into a buffer if it is not buffer-backed yet. Called before a pointer
    /// is handed to code that may outlive the array (a view, DLPack export).
    void ensureBuffer();

    // ---- Object protocol -----------------------------------------------------------------

    bool equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(camel::core::mm::IAllocator &allocator, const type::Type *type, bool deep = false)
        const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override;
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;
    void finalize() noexcept override;

  private:
    static constexpr size_t kInlineSlots = 10;

    Array() = default;

    /// Builds a rank-1 slot array of `initSize` null slots.
    static Array *
    createSlots(camel::core::mm::IAllocator &allocator, size_t initSize, size_t capacity);
    /// A slot array of any rank.
    static Array *createSlotsRank(
        camel::core::mm::IAllocator &allocator, std::span<const int64_t> extents,
        size_t capacity);
    /// Builds a compact array; `buffer` may be null, in which case the data is inline when it
    /// fits and a fresh buffer otherwise.
    static Array *createCompact(
        camel::core::mm::IAllocator &allocator, camel::core::type::TypeCode elemType,
        std::span<const int64_t> extents, Buffer *buffer, size_t byteOffset, bool zeroInit,
        bool view);

    void initLayout(std::span<const int64_t> extents);
    void noteFinalizable();
    void reallocateSlots(size_t newCapacity, const type::ArrayType *arrayType);
    void growCompact(uint64_t newCapacity);
    void releaseBuffer() noexcept;
    void recomputePointers();

    bool compactEqualsElements(const Array *other, bool deep) const;
    void printArray(
        std::ostream &os, const type::ArrayType *arrayType, size_t dim, uint64_t &flatIndex,
        const std::string &indent) const;
    void printTensor(std::ostream &os) const;
    void printTensorRecursive(std::ostream &os, size_t dim, uint64_t &flatIndex,
                              const std::string &indent) const;
    std::string formatTensorElement(uint64_t flatIndex) const;

    camel::core::mm::IAllocator *allocator_ = nullptr;
    Buffer *buffer_                         = nullptr;
    FixedArray *fixedArray_                 = nullptr; // Slots that did not fit inline
    slot_t inlineData_[kInlineSlots]        = {};
    std::byte *dataPtr_                     = nullptr; // Active element data
    int64_t *extents_                       = nullptr; // rank_ extents (trailing)
    int64_t *strides_                       = nullptr; // rank_ strides in elements (trailing)

    uint64_t size_     = 0; // Extent of dimension 0
    uint64_t numel_    = 0; // Product of the extents
    uint64_t capacity_ = 0; // Slots: elements; compact: element capacity of the storage
    size_t offset_     = 0; // Byte offset into the buffer

    camel::core::type::TypeCode elemTypeCode_ = camel::core::type::TypeCode::Void;
    uint32_t rank_      = 1;
    uint8_t elementSize_ = sizeof(slot_t);
    Storage storage_    = Storage::Slots;
    bool view_          = false;
};