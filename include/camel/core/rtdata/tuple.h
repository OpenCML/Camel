/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Nov. 12, 2025
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/view.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/pure.h"

#include <algorithm>
#include <cstddef>
#include <new>

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

namespace camel::core::rtdata {
/// Defined in rtdata/tagged.cpp.
void printTaggedSlots(
    std::ostream &os, const slot_t *slots, const camel::core::type::TupleType *type);
} // namespace camel::core::rtdata

class Tuple : public rtdata::Object {
  public:
    Tuple(const Tuple &)            = delete;
    Tuple &operator=(const Tuple &) = delete;

    /// What a compact element view carries; see Struct::ViewData.
    struct ViewData {
        const std::byte *bytes         = nullptr;
        const type::DataLayout *layout = nullptr;
        const type::Type *type         = nullptr;
        size_t stride                  = 0;
    };

    static constexpr uint32_t kViewFlag = 0x80000000u;

    /// Create a Tuple instance.
    static Tuple *create(size_t slotCount, camel::core::mm::IAllocator &allocator) {
        size_t headerSize = sizeof(Tuple);
        size_t dataSize   = sizeof(slot_t) * slotCount;
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(Tuple));
        if (!memory)
            throw std::bad_alloc();

        Tuple *t = new (memory) Tuple(slotCount);
        std::fill(t->data_, t->data_ + slotCount, NullSlot);
        return t;
    }


    bool isView() const { return (size_ & kViewFlag) != 0; }
    const ViewData *viewData() const {
        return reinterpret_cast<const ViewData *>(
            reinterpret_cast<const std::byte *>(this) - sizeof(ViewData));
    }

    size_t size() const { return size_ & ~kViewFlag; }

    /// One field as the raw slot it is stored as, without the initialized check: a printer
    /// renders a value that may still be under construction (a collection a higher-order
    /// operator is filling) and shows its unwritten elements instead of aborting (H31).
    slot_t slotAt(size_t index) const {
        ASSERT(index < size(), "Index out of range");
        if (isView()) {
            const ViewData *view = viewData();
            if (view->layout->slotShaped) {
                // Every field is an 8-byte leaf: the bytes are slot data, so this is one load.
                return reinterpret_cast<const slot_t *>(view->bytes)[index];
            }
            const auto *tupleType = static_cast<const type::TupleType *>(view->type);
            ASSERT(index < tupleType->size(), "Index out of range");
            const type::Type *fieldType = tupleType->typeAt(index);
            const std::byte *at = view->bytes + view->layout->fieldOffsets[index];
            if (fieldType->code() == type::TypeCode::Struct ||
                fieldType->code() == type::TypeCode::Tuple) {
                const type::DataLayout *layout = type::pureDataLayout(fieldType);
                ASSERT(layout != nullptr, "A compact field has a layout");
                return rtdata::pureViewOf(at, fieldType, layout, layout->size);
            }
            return rtdata::readPureScalar(at, fieldType->code());
        }
        return data_[index];
    }

    template <typename T> T get(size_t index) const {
        return rtdata::fromSlot<T>(slotAt(index));
    }

    template <typename T> void set(size_t index, T value) {
        ASSERT(index < size(), "Index out of range");
        data_[index] = rtdata::toSlot(value);
    }

    template <typename T> void set(size_t index, T value, const type::TupleType *tupleType) {
        ASSERT(index < size(), "Index out of range");
        ASSERT(tupleType != nullptr && index < tupleType->size(), "TupleType is invalid.");
        // A view is not a value: materialise it at the store boundary (see Struct::set). Only
        // an aggregate field can hold one, so the common scalar write pays no call.
        const type::Type *fieldType = tupleType->typeAt(index);
        slot_t slot                 = rtdata::toSlot(value);
        if (fieldType->code() == type::TypeCode::Struct ||
            fieldType->code() == type::TypeCode::Tuple) {
            slot = rtdata::materializeIfView(slot, fieldType);
        }
        // Debug builds: after materialisation, a view reaching the slot is an escape.
        CAMEL_ASSERT_NOT_VIEW(slot, fieldType, "tuple field");
        camel::core::mm::writeBarrier(this, tupleType, slot, fieldType);
        data_[index] = slot;
    }

    slot_t *data() {
        ASSERT(!isView(), "A compact element view has no slot array");
        return data_;
    }
    const slot_t *data() const {
        ASSERT(!isView(), "A compact element view has no slot array");
        return data_;
    }

    /// Where the slot data of a real tuple begins: the end of this header (aggregate.h).
    static constexpr size_t slotDataOffset() noexcept { return sizeof(Tuple); }

    /// For a variable cell (TupleType::cell) only: whether the array in its slot is referred
    /// to by this cell and by nothing else, so that the variable may grow it in place (the
    /// rebinding `acc = acc->append<v>`, :op/assn_owned). Only the in-place assignment sets
    /// it, after it stored an array made by :var/take and an in-place form; every read of the
    /// variable and every other write clears it (ops.cpp), because from then on another
    /// reference may exist. A new tuple, and a clone, start without it. Not part of the value:
    /// equals() and print() ignore it.
    bool holdsUnsharedArray() const { return unsharedArray_ != 0; }
    void setHoldsUnsharedArray(bool unshared) { unsharedArray_ = unshared ? 1 : 0; }

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Tuple, "Type must be TupleType");
        if (!isOfSameCls(this, other))
            return false;

        const type::TupleType *tupleType = static_cast<const type::TupleType *>(type);
        const Tuple *otherTuple          = reinterpret_cast<const Tuple *>(other);
        if (size() != otherTuple->size() || size() != tupleType->size())
            return false;

        if (isView() || otherTuple->isView()) {
            rtdata::Object *left =
                isView() ? rtdata::materializeAggregate(
                               viewData()->bytes, type, camel::core::mm::autoSpace())
                         : const_cast<Tuple *>(this);
            rtdata::Object *right =
                otherTuple->isView()
                    ? rtdata::materializeAggregate(
                          otherTuple->viewData()->bytes, type, camel::core::mm::autoSpace())
                    : const_cast<Tuple *>(otherTuple);
            return left->equals(right, type, true);
        }

        auto codes          = tupleType->codes();
        const slot_t *dataA = data_;
        const slot_t *dataB = otherTuple->data_;

        if (deep) {
            // Deep compare: recurse for reference types; compare values directly for plain types.
            for (size_t i = 0; i < size(); ++i) {
                if (type::isGCTraced(codes[i])) {
                    const rtdata::Object *refA =
                        reinterpret_cast<const rtdata::Object *const *>(dataA)[i];
                    const rtdata::Object *refB =
                        reinterpret_cast<const rtdata::Object *const *>(dataB)[i];
                    if (refA == refB)
                        continue;
                    // A reference slot may be null (the error slot of a `T!`).
                    if (!refA || !refB)
                        return false;
                    if (!refA->equals(refB, tupleType->typeAt(i), true))
                        return false;
                } else {
                    if (dataA[i] != dataB[i])
                        return false;
                }
            }
            return true;
        } else {
            // Shallow compare: compare slots as-is (for references, compare pointer addresses
            // only).
            return std::memcmp(dataA, dataB, size() * sizeof(slot_t)) == 0;
        }
    }

    virtual rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override {
        ASSERT(type && type->code() == type::TypeCode::Tuple, "Type must be TupleType");
        if (isView()) {
            return rtdata::materializeAggregate(viewData()->bytes, type, allocator);
        }
        const type::TupleType *tupleType = static_cast<const type::TupleType *>(type);
        auto codes                       = tupleType->codes();

        Tuple *newTuple   = Tuple::create(size(), allocator);
        const slot_t *src = data_;
        slot_t *dst       = newTuple->data_;

        for (size_t i = 0; i < size(); ++i) {
            if (type::isGCTraced(codes[i])) {
                const rtdata::Object *oriRef =
                    reinterpret_cast<const rtdata::Object *const *>(src)[i];
                rtdata::Object *newRef = rtdata::NullRef;

                if (oriRef) {
                    if (deep) {
                        // Deep copy: recursively clone referenced objects.
                        newRef = oriRef->clone(allocator, tupleType->typeAt(i), true);
                    } else {
                        // Shallow copy: copy the reference pointer only.
                        newRef = const_cast<rtdata::Object *>(oriRef);
                    }
                }
                reinterpret_cast<rtdata::Object **>(dst)[i] = newRef;
                camel::core::mm::writeBarrier(newTuple, tupleType, newRef, tupleType->typeAt(i));
            } else {
                // Non-reference types: copy the slot data directly.
                dst[i] = src[i];
            }
        }

        return reinterpret_cast<rtdata::Object *>(newTuple);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override {
        ASSERT(type && type->code() == type::TypeCode::Tuple, "Type must be TupleType");
        const type::TupleType *tupleType = static_cast<const type::TupleType *>(type);
        if (tupleType->isTagged()) {
            // `T?` / `T!`: the value or the error, not the box (rtdata/tagged.h).
            ASSERT(!isView(), "A tagged tuple is not compact pure data");
            rtdata::printTaggedSlots(os, data_, tupleType);
            return;
        }

        os << "(";

        for (size_t i = 0; i < size(); ++i) {
            if (i > 0)
                os << ", ";
            rtdata::printSlot(os, get<slot_t>(i), tupleType->typeAt(i));
        }

        os << ")";
    }

    virtual void onMoved() override {}

    virtual void
    updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override {
        if (isView()) {
            return; // pure data has no references
        }
        if (!type || type->code() != type::TypeCode::Tuple)
            return;
        const type::TupleType *tupleType = static_cast<const type::TupleType *>(type);
        auto codes                       = tupleType->codes();
        rtdata::Object **refArr          = reinterpret_cast<rtdata::Object **>(data_);

        for (size_t i = 0; i < size(); ++i) {
            if (type::isGCTraced(codes[i])) {
                if (rtdata::Object *&ref = refArr[i]) {
                    const type::Type *slotType = tupleType->typeAt(i);
                    ref                        = relocate(
                        ref,
                        slotType,
                        rtdata::RefTraceInfo{
                            .owner     = this,
                            .ownerType = type,
                            .slotType  = slotType,
                            .ownerKind = "Tuple",
                            .slotName  = {},
                            .slotIndex = i,
                        });
                }
            }
        }
    }

    /// Only the view ring (rtdata/view.cpp) passes flags; ordinary code uses create().
    explicit Tuple(size_t slotCount, uint32_t flags = 0)
        : size_(static_cast<uint32_t>(slotCount) | flags) {}

  private:
    uint32_t size_;
    // See holdsUnsharedArray(). It takes the four bytes of padding between size_ and data_,
    // so a tuple has the same layout and size as before it was added.
    uint32_t unsharedArray_ = 0;
    slot_t data_[];
};

static_assert(
    sizeof(Tuple) == Tuple::slotDataOffset(), "Tuple slot data starts after its header.");
static_assert(
    alignof(Tuple) == alignof(slot_t),
    "Tuple alignment must be a slot's: aggregateSlots reads slots at the data offset.");