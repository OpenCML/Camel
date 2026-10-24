// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The runtime array value.
 *
 * An array is an element type plus a shape, and two storage classes (design:
 * docs/design/arrays-memory/README.cn.md 3.1, 3.2):
 *
 * - Slots: one 8-byte slot per element, able to hold a reference. This is the representation
 *   for element types that are not pure data (strings, nested arrays, closures, tagged forms).
 * - Compact: a contiguous row-major run of pure data (type/pure.h): numbers, booleans, complex
 *   numbers, and structs and tuples made only of them. Each element occupies the bytes of its
 *   aggregate layout, so an array of structs has no per-element Struct object. The run is a
 *   Block (core/mm/block.h) when it is larger than mm::kInlineBlockBytes or when a view or an
 *   export needs a stable address; otherwise its bytes are inline in the GC object and move
 *   with it (no block, no atomic count, no finalizer: small logic programs do not pay for the
 *   new memory management).
 *
 * The decision is made once, by type::arrayStorageFor: a pure-data element type is compact and
 * anything else is slots. "A compact block holds pure data only" is the leaf rule that makes
 * reference counting safe and zero-copy exchange possible. DLPack exports only arrays whose
 * element type is a numeric scalar; arrays of structs are left to the Arrow item.
 *
 * An inline array is promoted to a Block when a view is made of it or when it is exported
 * (DLPack): both hand a pointer to code that may outlive the array, and the GC object may
 * move. Promotion copies once, after which the data address is stable.
 *
 * Arrays are immutable values (design principles 5.4): "modifying" an operation returns a new
 * value, except that an operation may update in place when nothing else can see the old value
 * (design principles 10.5). `reusableBlock` is the hook for that: it gives the operation the
 * array's own block when the count is 1 and the shape matches, and a fresh one otherwise.
 */

#pragma once

#include "base.h"
#include "camel/core/mm/block.h"
#include "camel/core/rtdata/aggregate.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/rtdata/view.h"
#include "camel/core/type/pure.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <new>
#include <ostream>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

namespace camel::core::type {
struct DataLayout;
}

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
    using Block = camel::core::mm::Block;
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
    /// The view shares the block; an inline source is promoted (copied once) first. Only
    /// compact arrays can be viewed.
    static Array *
    view(Array *source, std::span<const int64_t> extents, uint64_t elementOffset = 0);

    /// An array over `block`'s bytes (DLPack import, external memory). The array retains the
    /// block; `byteOffset` is in bytes and `view` marks the storage as not owned for growth.
    static Array *wrap(
        camel::core::mm::IAllocator &allocator, const type::ArrayType *arrayType,
        std::span<const int64_t> extents, Block *block, size_t byteOffset = 0,
        bool view = false);

    // ---- Shape ----------------------------------------------------------------------------

    Storage storage() const { return storage_; }
    bool isCompact() const { return storage_ == Storage::Compact; }
    bool isSlots() const { return storage_ == Storage::Slots; }
    /// True when this value reads another array's block rather than owning the storage.
    bool isView() const { return view_; }

    /// Whether this array may be changed in place: an inline compact array is its own storage,
    /// a block-backed one needs the only reference to a Camel-owned block at offset 0 and
    /// must not be a view. An imported (external) block is never writable (design principles
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

    /// The element type this value stores, or null when the array was created without one
    /// (an array literal before its type is known). Interned types live for the process, so
    /// the pointer stays valid.
    const camel::core::type::Type *elemType() const { return elemType_; }
    /// The element type code this value stores (Void for a slot array created without one).
    camel::core::type::TypeCode elemTypeCode() const { return elemTypeCode_; }
    /// The name `tensor` uses for the element type.
    camel::core::type::TypeCode dtype() const { return elemTypeCode_; }
    size_t elementSize() const { return elementSize_; }
    /// True when the element is a struct or tuple stored by its layout rather than by a slot
    /// that points at a Struct/Tuple object.
    bool isAggregateElement() const {
        return elemType_ != nullptr &&
               (elemType_->code() == camel::core::type::TypeCode::Struct ||
                elemType_->code() == camel::core::type::TypeCode::Tuple);
    }
    /// True when one element is exactly the bytes of one slot (i64, f64): the type-level test
    /// that the slot fast paths may use. c64 is 8 bytes but is not slot-resident, so it must
    /// not be read as a slot.
    bool isSlotResidentElement() const {
        const camel::core::type::DataLayout *layout =
            elemType_ != nullptr ? camel::core::type::pureDataLayout(elemType_) : nullptr;
        return layout != nullptr && layout->slotResident;
    }

    // ---- Storage access ------------------------------------------------------------------

    /// Slot data. Only a slot array and an 8-byte scalar compact element type have this layout;
    /// use dataAs<T>() or the get/set accessors otherwise.
    slot_t *data() {
        ASSERT(
            isSlots() || isSlotResidentElement(),
            "Use dataAs<T>() for this element type");
        return reinterpret_cast<slot_t *>(dataPtr_);
    }
    const slot_t *data() const {
        ASSERT(
            isSlots() || isSlotResidentElement(),
            "Use dataAs<T>() for this element type");
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
    slot_t getSlot(uint64_t flatIndex) const {
        ASSERT(flatIndex < numel_, "Array flat index out of range");
        if (isSlots()) {
            return reinterpret_cast<const slot_t *>(dataPtr_)[flatIndex];
        }
        if (!isAggregateElement() && isSlotResidentElement() &&
            elementSize_ == sizeof(slot_t)) {
            return reinterpret_cast<const slot_t *>(dataPtr_)[flatIndex];
        }
        return getSlotSlow(flatIndex);
    }
    /// One element for an array consumer. For a compact pure-data struct/tuple this is a view
    /// into the array's block (no allocation, no materialised Struct/Tuple); every other
    /// element is getSlot. The array must be block-backed first: call ensureBlock() on it
    /// before iterating, so the bytes do not move at a callback's safepoint. A view must not
    /// be stored; the store paths materialise it (rtdata/view.h).
    slot_t getElementView(
        uint64_t flatIndex, const camel::core::type::DataLayout *layout = nullptr) const {
        ASSERT(flatIndex < numel_, "Array flat index out of range");
        if (isCompact() && isAggregateElement() && block_ != nullptr) {
            if (layout == nullptr) {
                layout = camel::core::type::pureDataLayout(elemType_);
            }
            return camel::core::rtdata::pureViewOf(
                elementPtr(flatIndex), elemType_, layout, elementSize_);
        }
        return getSlot(flatIndex);
    }
    slot_t getSlotSlow(uint64_t flatIndex) const;
    /// Stores one element from a slot. `arrayType` supplies the element type for the reference
    /// write barrier (slot arrays only; a compact write never needs one) and may be null for a
    /// compact array, whose own element type is enough.
    void setSlot(uint64_t flatIndex, slot_t value, const camel::core::type::ArrayType *arrayType = nullptr) {
        // `appendSlot` stores the new element at `numel_` before it increments the count, so the
        // bound is inclusive here; the capacity has already been grown for that write. The bound
        // used to be exclusive, which aborted every compact append in a Debug build (H30).
        if (isCompact() && slotShaped_ && value != NullSlot &&
            !camel::core::rtdata::isViewSlot(value, elemType_)) {
            ASSERT(flatIndex <= numel_, "Array flat index out of range");
            std::memcpy(
                dataPtr_ + flatIndex * elementSize_,
                camel::core::rtdata::aggregateSlots(value), elementSize_);
            return;
        }
        if (isCompact() && isSlotResidentElement() && elementSize_ == sizeof(slot_t)) {
            ASSERT(flatIndex <= numel_, "Array flat index out of range");
            reinterpret_cast<slot_t *>(dataPtr_)[flatIndex] = value;
            return;
        }
        setSlotSlow(flatIndex, value, arrayType);
    }
    void setSlotSlow(
        uint64_t flatIndex, slot_t value,
        const camel::core::type::ArrayType *arrayType = nullptr);

    // ---- Element loops --------------------------------------------------------------------
    //
    // getSlot/setSlot decide the storage class and the element width at every element. That is
    // right for a single access and wrong for a loop: map/filter/reduce/foreach over n elements
    // asked the same two questions n times. A loop asks once (elementRead/elementWrite) and
    // reads and writes through readElement/writeElement, which are compiled per class.
    //
    // What a loop may hoist is the class decision, never a pointer: the element callback is a
    // safepoint, so the array object (and, for an inline compact array, its bytes) can move
    // between two elements. Every loop therefore re-reads its arrays on every round and passes
    // them to these functions.

    /// How a loop reads this array's elements, decided once for the call.
    enum class ElementRead : uint8_t {
        Direct, // a slot array, or a compact element of exactly one slot: the slot_t layout
        View,   // a compact pure-data aggregate: a view into the block, never materialised
        Slow,   // any other compact element: taken out of its bytes by getSlotSlow
    };
    /// How a loop writes them. Writing needs one distinction reading does not: a slot array may
    /// hold a reference, so its store goes through the write barrier.
    enum class ElementWrite : uint8_t {
        Direct,    // a compact element of exactly one slot: a plain store
        Aggregate, // a compact pure-data aggregate: its bytes
        Slow,      // a slot array (the barrier) or an element of any width but one slot
    };

    ElementRead elementRead() const {
        if (isSlots() || (!isAggregateElement() && elementSize_ == sizeof(slot_t))) {
            return ElementRead::Direct;
        }
        return isAggregateElement() ? ElementRead::View : ElementRead::Slow;
    }
    ElementWrite elementWrite() const {
        if (isSlots()) {
            return ElementWrite::Slow;
        }
        if (isAggregateElement()) {
            return ElementWrite::Aggregate;
        }
        return elementSize_ == sizeof(slot_t) ? ElementWrite::Direct : ElementWrite::Slow;
    }

    /// One element, with the class already decided by elementRead. A View read needs the array
    /// block-backed (ensureBlock) and the element `layout`, which the loop takes once for all
    /// elements; it returns a view into the block, as getElementView does.
    template <ElementRead R>
    slot_t readElement(
        uint64_t flatIndex,
        const camel::core::type::DataLayout *layout = nullptr) const {
        ASSERT(flatIndex < numel_, "Array flat index out of range");
        if constexpr (R == ElementRead::Direct) {
            return reinterpret_cast<const slot_t *>(dataPtr_)[flatIndex];
        } else if constexpr (R == ElementRead::View) {
            ASSERT(
                block_ != nullptr,
                "A view read needs a block-backed array; call ensureBlock() before the loop.");
            return camel::core::rtdata::pureViewOf(
                dataPtr_ + flatIndex * elementSize_, elemType_,
                layout != nullptr ? layout : camel::core::type::pureDataLayout(elemType_),
                elementSize_);
        } else {
            return getSlotSlow(flatIndex);
        }
    }
    /// Stores one element, with the class already decided by elementWrite.
    template <ElementWrite W>
    void writeElement(
        uint64_t flatIndex, slot_t value,
        const camel::core::type::ArrayType *arrayType = nullptr) {
        if constexpr (W == ElementWrite::Direct) {
            ASSERT(flatIndex < numel_, "Array flat index out of range");
            reinterpret_cast<slot_t *>(dataPtr_)[flatIndex] = value;
        } else if constexpr (W == ElementWrite::Aggregate) {
            // setSlot's fast path for a slot-shaped aggregate, with the storage-class tests
            // already folded into the class.
            if (slotShaped_ && value != NullSlot &&
                !camel::core::rtdata::isViewSlot(value, elemType_)) {
                ASSERT(flatIndex < numel_, "Array flat index out of range");
                std::memcpy(
                    dataPtr_ + flatIndex * elementSize_,
                    camel::core::rtdata::aggregateSlots(value), elementSize_);
            } else {
                setSlotSlow(flatIndex, value, arrayType);
            }
        } else {
            setSlotSlow(flatIndex, value, arrayType);
        }
    }

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

    // ---- Size and growth (slot arrays, and compact arrays whose block can grow) ----------

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

    /// A block the caller may write and adopt as this array's storage: this array's own
    /// block when it is compact, unique, and already the right size; a fresh block (copied
    /// from this array) otherwise. This is the hook for the in-place guarantee (design
    /// principles 10.5): an operation that returns "a new array" writes into the returned
    /// block. `newBytes` is the required data length.
    Block *reusableBlock(size_t newBytes);
    /// Makes this array block-backed and (re)points its data at `block`, releasing the old
    /// one. Used by in-place operations after a reusableBlock() call.
    void adoptBlock(Block *block, size_t newBytes);
    /// The array's current block, or null for a slot array / an inline compact array.
    Block *block() const { return block_; }
    /// Copies inline data into a block if it is not block-backed yet. Called before a pointer
    /// is handed to code that may outlive the array (a view, DLPack export).
    void ensureBlock();

    // ---- Object protocol -----------------------------------------------------------------

    bool equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override;
    rtdata::Object *
    clone(camel::core::mm::IAllocator &allocator, const type::Type *type, bool deep = false)
        const override;
    void print(std::ostream &os, const type::Type *type) const override;
    void onMoved() override;
    void updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override;
    void finalize() noexcept override;
    Block *heldBlock() const noexcept override;

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
    /// Builds a compact array; `block` may be null, in which case the data is inline when it
    /// fits and a fresh block otherwise.
    static Array *createCompact(
        camel::core::mm::IAllocator &allocator, const camel::core::type::Type *elemType,
        std::span<const int64_t> extents, Block *block, size_t byteOffset, bool zeroInit,
        bool view);

    void initLayout(std::span<const int64_t> extents);
    void noteFinalizable();
    /// Tells the allocator that this array now holds `block`. A collector that paces promoted
    /// block bytes registers the block at the takeover point, so an array that is already in
    /// the elder space when it gets a block (a promoted array growing) counts like a block
    /// promoted with a value. Other allocators ignore it.
    void noteBlockHeld(Block *block);
    void reallocateSlots(size_t newCapacity, const type::ArrayType *arrayType);
    void growCompact(uint64_t newCapacity);
    void releaseBlock() noexcept;
    void recomputePointers();

    bool compactEqualsElements(const Array *other, bool deep) const;
    /// Prints the elements as nested brackets. `elemType` is the type the elements print by: the
    /// declared element type, or — when the declaration does not fix one, as the numeric-unknown
    /// array `Tensor` does not — the type the value holds (`elemType_`).
    void printArray(
        std::ostream &os, const type::Type *elemType, size_t dim, uint64_t &flatIndex) const;

    camel::core::mm::IAllocator *allocator_ = nullptr;
    Block *block_                         = nullptr;
    FixedArray *fixedArray_                 = nullptr; // Slots that did not fit inline
    slot_t inlineData_[kInlineSlots]        = {};
    std::byte *dataPtr_                     = nullptr; // Active element data
    int64_t *extents_                       = nullptr; // rank_ extents (trailing)
    int64_t *strides_                       = nullptr; // rank_ strides in elements (trailing)

    uint64_t size_     = 0; // Extent of dimension 0
    uint64_t numel_    = 0; // Product of the extents
    uint64_t capacity_ = 0; // Slots: elements; compact: element capacity of the storage
    size_t offset_     = 0; // Byte offset into the block

    camel::core::type::TypeCode elemTypeCode_ = camel::core::type::TypeCode::Void;
    /// The interned element type, kept for the layout of aggregate elements and for the
    /// runtime paths that need more than the code. Null when the array was created without a
    /// type (a slot array).
    const camel::core::type::Type *elemType_ = nullptr;
    uint32_t rank_      = 1;
    uint32_t elementSize_ = sizeof(slot_t);
    Storage storage_    = Storage::Slots;
    bool view_          = false;
    /// The compact element is a slot-shaped aggregate (see DataLayout): a real Struct/Tuple can
    /// be copied into its bytes without a field loop.
    bool slotShaped_    = false;
};

/**
 * Stores the value a reduce keeps as its accumulator (H29): the one store after which the value
 * is read again in a later round, so it must be a value and not a view of the element that round
 * read. The caller materialises a view first (`materializeCallResult`), which is what the bytecode
 * VM already did; the assertion is the check that it did. In a release build the assertion is
 * compiled out, and `setSlot` would also materialise a view, so only a debug build can show that a
 * caller forgot. Both the nvm reduce operator and the view self-test store through here.
 */
inline void storeReduceAccumulator(
    Array *cell, slot_t value, const type::ArrayType *cellType) {
    ASSERT(cell != nullptr, "The reduce accumulator cell is null.");
    ASSERT(cellType != nullptr, "The reduce accumulator needs the array type of its cell.");
    CAMEL_ASSERT_NOT_VIEW(value, cellType->elemType(), "the reduce accumulator");
    cell->set<slot_t>(0, value, cellType);
}

// The one place that must stay in step with Array's element classes: a loop dispatches through
// these instead of writing the switch itself, so adding a class is a change here and nowhere
// else. `body` receives the class as an integral_constant, which is what makes the loop body
// compile per class.
template <typename Body>
void dispatchElementAccess(
    Array::ElementRead read, Array::ElementWrite write, Body &&body) {
    using R = Array::ElementRead;
    using W = Array::ElementWrite;
    switch (read) {
    case R::Direct:
        switch (write) {
        case W::Direct:
            body(std::integral_constant<R, R::Direct>{}, std::integral_constant<W, W::Direct>{});
            return;
        case W::Aggregate:
            body(
                std::integral_constant<R, R::Direct>{},
                std::integral_constant<W, W::Aggregate>{});
            return;
        case W::Slow:
            body(std::integral_constant<R, R::Direct>{}, std::integral_constant<W, W::Slow>{});
            return;
        }
        return;
    case R::View:
        switch (write) {
        case W::Direct:
            body(std::integral_constant<R, R::View>{}, std::integral_constant<W, W::Direct>{});
            return;
        case W::Aggregate:
            body(std::integral_constant<R, R::View>{}, std::integral_constant<W, W::Aggregate>{});
            return;
        case W::Slow:
            body(std::integral_constant<R, R::View>{}, std::integral_constant<W, W::Slow>{});
            return;
        }
        return;
    case R::Slow:
        switch (write) {
        case W::Direct:
            body(std::integral_constant<R, R::Slow>{}, std::integral_constant<W, W::Direct>{});
            return;
        case W::Aggregate:
            body(std::integral_constant<R, R::Slow>{}, std::integral_constant<W, W::Aggregate>{});
            return;
        case W::Slow:
            body(std::integral_constant<R, R::Slow>{}, std::integral_constant<W, W::Slow>{});
            return;
        }
        return;
    }
}

/// dispatchElementAccess for a loop that only reads (filter, reduce, foreach, and the source of
/// a map whose result is built another way).
template <typename Body> void dispatchElementRead(Array::ElementRead read, Body &&body) {
    using R = Array::ElementRead;
    switch (read) {
    case R::Direct:
        body(std::integral_constant<R, R::Direct>{});
        return;
    case R::View:
        body(std::integral_constant<R, R::View>{});
        return;
    case R::Slow:
        body(std::integral_constant<R, R::Slow>{});
        return;
    }
}