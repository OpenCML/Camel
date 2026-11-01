// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
    /// operator is filling) and shows its unwritten elements instead of aborting.
    slot_t slotAt(size_t index) const {
        GUARD(
            index < size(),
            std::format(
                "Tuple field out of range: field {} of a tuple with {} fields",
                index,
                size()));
        if (isView()) {
            const ViewData *view = viewData();
            if (view->layout->slotShaped) {
                // Every field is an 8-byte leaf: the bytes are slot data, so this is one load.
                return reinterpret_cast<const slot_t *>(view->bytes)[index];
            }
            const auto *tupleType = static_cast<const type::TupleType *>(view->type);
            GUARD(
                index < tupleType->size(),
                std::format(
                    "Tuple field out of range: field {} of the {} fields this view stores",
                    index,
                    tupleType->size()));
            const type::Type *fieldType = tupleType->typeAt(index);
            const std::byte *at = view->bytes + view->layout->fieldOffsets[index];
            if (fieldType->code() == type::TypeCode::Struct ||
                fieldType->code() == type::TypeCode::Tuple) {
                const type::DataLayout *layout = type::pureDataLayout(fieldType);
                GUARD(
                    layout != nullptr,
                    std::format(
                        "A compact field of type '{}' has a layout, and this one has none: the "
                        "field is stored by its bytes and cannot be read without them",
                        fieldType->toString()));
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
        GUARD(
            index < size(),
            std::format(
                "Tuple field out of range: a write at field {} of a tuple with {} fields",
                index,
                size()));
        data_[index] = rtdata::toSlot(value);
    }

    template <typename T> void set(size_t index, T value, const type::TupleType *tupleType) {
        GUARD(
            index < size(),
            std::format(
                "Tuple field out of range: a write at field {} of a tuple with {} fields",
                index,
                size()));
        GUARD(
            tupleType != nullptr && index < tupleType->size(),
            std::format(
                "TupleType is invalid for this write: the field is {} and the type passed "
                "describes {}",
                index,
                tupleType == nullptr ? std::string("no fields")
                                     : std::to_string(tupleType->size()) + " fields"));
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
        GUARD(
            !isView(),
            "A compact element view has no slot array: its bytes belong to another value, so "
            "there are no slots here to hand out");
        return data_;
    }
    const slot_t *data() const {
        GUARD(
            !isView(),
            "A compact element view has no slot array: its bytes belong to another value, so "
            "there are no slots here to hand out");
        return data_;
    }

    /// Where the slot data of a real tuple begins: the end of this header (aggregate.h).
    static constexpr size_t slotDataOffset() noexcept { return sizeof(Tuple); }

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        GUARD(
            type != nullptr && type->code() == type::TypeCode::Tuple,
            std::format(
                "Type must be TupleType, and this call passed {}: the fields are read and "
                "written through the tuple type's field types",
                type == nullptr
                    ? std::string("none")
                    : "code " + std::to_string(static_cast<int>(type->code()))));
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
        GUARD(
            type != nullptr && type->code() == type::TypeCode::Tuple,
            std::format(
                "Type must be TupleType, and this call passed {}: the fields are read and "
                "written through the tuple type's field types",
                type == nullptr
                    ? std::string("none")
                    : "code " + std::to_string(static_cast<int>(type->code()))));
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
        GUARD(
            type != nullptr && type->code() == type::TypeCode::Tuple,
            std::format(
                "Type must be TupleType, and this call passed {}: the fields are read and "
                "written through the tuple type's field types",
                type == nullptr
                    ? std::string("none")
                    : "code " + std::to_string(static_cast<int>(type->code()))));
        const type::TupleType *tupleType = static_cast<const type::TupleType *>(type);
        if (tupleType->isTagged()) {
            // `T?` / `T!`: the value or the error, not the box (rtdata/tagged.h).
            GUARD(
                !isView(),
                "A tagged tuple is not compact pure data: a view has no tag bits of its own, so "
                "printing it would read the wrong field");
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
    slot_t data_[];
};

static_assert(
    sizeof(Tuple) == Tuple::slotDataOffset(), "Tuple slot data starts after its header.");
static_assert(
    alignof(Tuple) == alignof(slot_t),
    "Tuple alignment must be a slot's: aggregateSlots reads slots at the data offset.");