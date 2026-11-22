// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "base.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/view.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/pure.h"

#include <algorithm>
#include <cstddef>
#include <new>

namespace rtdata = camel::core::rtdata;
namespace type   = camel::core::type;

class Struct : public rtdata::Object {
  public:
    Struct(const Struct &)            = delete;
    Struct &operator=(const Struct &) = delete;

    /// What a compact element view carries: the bytes of one element (inside the array's
    /// block, stable across a collection), their layout and type.
    struct ViewData {
        const std::byte *bytes         = nullptr;
        const type::DataLayout *layout = nullptr;
        const type::Type *type         = nullptr;
        size_t stride                  = 0;
    };

    /// A view is marked in the high bit of the field count. Normal structs keep their layout.
    static constexpr uint32_t kViewFlag = 0x80000000u;

    static Struct *create(size_t fieldCount, camel::core::mm::IAllocator &allocator) {
        size_t headerSize = sizeof(Struct);
        size_t dataSize   = sizeof(slot_t) * fieldCount;
        size_t totalSize  = headerSize + dataSize;

        void *memory = allocator.alloc(totalSize, alignof(Struct));
        if (!memory)
            throw std::bad_alloc();

        Struct *s = new (memory) Struct(fieldCount);

        // Standard slot container: initialize everything to NullSlot (references are null;
        // value types are overwritten by set).
        std::fill(s->data_, s->data_ + fieldCount, NullSlot);

        return s;
    }


    bool isView() const { return (size_ & kViewFlag) != 0; }
    const ViewData *viewData() const {
        return reinterpret_cast<const ViewData *>(
            reinterpret_cast<const std::byte *>(this) - sizeof(ViewData));
    }

    /// Where the slot data of a real struct begins: the end of this header. The array store
    /// path asks this instead of computing the header layout itself (aggregate.h).
    static constexpr size_t slotDataOffset() noexcept { return sizeof(Struct); }

    size_t size() const { return size_ & ~kViewFlag; }

    bool has(std::string_view name, const type::Type *type) const {
        GUARD(
            type != nullptr && type->code() == type::TypeCode::Struct,
            std::format(
                "Type must be StructType, and this call passed {}: the fields are read and "
                "written through the struct type's field types",
                type == nullptr
                    ? std::string("none")
                    : "code " + std::to_string(static_cast<int>(type->code()))));
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        return structType->findField(name).has_value();
    }

    /// One field as the raw slot it is stored as, without the initialized check: a printer
    /// renders a value that may still be under construction (a collection a higher-order
    /// operator is filling) and shows its unwritten elements instead of aborting.
    slot_t slotAt(size_t index) const {
        GUARD(
            index < size(),
            std::format(
                "Struct field out of range: field {} of a value with {} fields", index, size()));
        if (isView()) {
            const ViewData *view = viewData();
            if (view->layout->slotShaped) {
                // Every field is an 8-byte leaf: the bytes are slot data, so this is one load.
                return reinterpret_cast<const slot_t *>(view->bytes)[index];
            }
            const auto *structType = static_cast<const type::StructType *>(view->type);
            GUARD(
                index < structType->size(),
                std::format(
                    "Struct field out of range: field {} of the {} fields this view stores",
                    index,
                    structType->size()));
            const type::Type *fieldType = structType->typeAt(index);
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

    template <typename T> T get(std::string_view name, const type::Type *type) const {
        GUARD(
            type != nullptr && type->code() == type::TypeCode::Struct,
            std::format(
                "Type must be StructType, and this call passed {}: the fields are read and "
                "written through the struct type's field types",
                type == nullptr
                    ? std::string("none")
                    : "code " + std::to_string(static_cast<int>(type->code()))));
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        auto optIndex                      = structType->findField(name);
        GUARD(
            optIndex.has_value(),
            std::format(
                "Field name not found: '{}' names no field of '{}'", name, type->toString()));
        return get<T>(optIndex.value());
    }

    template <typename T> void set(size_t index, T value) {
        GUARD(
            index < size(),
            std::format(
                "Struct field out of range: a write at field {} of a value with {} fields",
                index,
                size()));
        data_[index] = rtdata::toSlot(value);
    }

    template <typename T> void set(size_t index, T value, const type::StructType *structType) {
        GUARD(
            index < size(),
            std::format(
                "Struct field out of range: a write at field {} of a value with {} fields",
                index,
                size()));
        GUARD(
            structType != nullptr && index < structType->size(),
            std::format(
                "StructType is invalid for this write: the field is {} and the type passed "
                "describes {}",
                index,
                structType == nullptr ? std::string("no fields")
                                      : std::to_string(structType->size()) + " fields"));
        // A view is not a value: if one reaches a real store, turn it into a real Struct/Tuple
        // here. This is the boundary the read path relies on (map/filter/reduce/foreach). Only
        // an aggregate field can hold one, so the common scalar write pays no call.
        const type::Type *fieldType = structType->typeAt(index);
        slot_t slot                 = rtdata::toSlot(value);
        if (fieldType->code() == type::TypeCode::Struct ||
            fieldType->code() == type::TypeCode::Tuple) {
            slot = rtdata::materializeIfView(slot, fieldType);
        }
        // Debug builds: after materialisation, a view reaching the slot is an escape.
        CAMEL_ASSERT_NOT_VIEW(slot, fieldType, "struct field");
        camel::core::mm::writeBarrier(this, structType, slot, fieldType);
        data_[index] = slot;
    }

    template <typename T> void set(std::string_view name, T value, const type::Type *type) {
        GUARD(
            type != nullptr && type->code() == type::TypeCode::Struct,
            std::format(
                "Type must be StructType, and this call passed {}: the fields are read and "
                "written through the struct type's field types",
                type == nullptr
                    ? std::string("none")
                    : "code " + std::to_string(static_cast<int>(type->code()))));
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        auto optIndex                      = structType->findField(name);
        GUARD(
            optIndex.has_value(),
            std::format(
                "Field name not found: '{}' names no field of '{}'", name, type->toString()));
        set<T>(optIndex.value(), value, structType);
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

    virtual bool
    equals(const rtdata::Object *other, const type::Type *type, bool deep = false) const override {
        GUARD(
            type != nullptr && type->code() == type::TypeCode::Struct,
            std::format(
                "Type must be StructType, and this call passed {}: the fields are read and "
                "written through the struct type's field types",
                type == nullptr
                    ? std::string("none")
                    : "code " + std::to_string(static_cast<int>(type->code()))));
        if (!isOfSameCls(this, other))
            return false;

        const type::StructType *structType = static_cast<const type::StructType *>(type);
        const Struct *otherStruct          = reinterpret_cast<const Struct *>(other);

        // Different field counts mean inequality.
        if (size() != otherStruct->size() || size() != structType->size())
            return false;

        if (isView() || otherStruct->isView()) {
            // Value equality of a view is the equality of the element it names. Materialising
            // here is fine: equals is not a per-element read path.
            rtdata::Object *left = isView()
                                       ? rtdata::materializeAggregate(
                                             viewData()->bytes, type, camel::core::mm::autoSpace())
                                       : const_cast<Struct *>(this);
            rtdata::Object *right =
                otherStruct->isView()
                    ? rtdata::materializeAggregate(
                          otherStruct->viewData()->bytes, type, camel::core::mm::autoSpace())
                    : const_cast<Struct *>(otherStruct);
            return left->equals(right, type, true);
        }

        auto codes          = structType->codes();
        const slot_t *dataA = data_;
        const slot_t *dataB = otherStruct->data_;

        if (!deep) {
            // Shallow compare: compare the entire memory block directly (reference fields
            // compare pointers only).
            return std::memcmp(dataA, dataB, size() * sizeof(slot_t)) == 0;
        }

        // Deep compare: recurse field by field.
        for (size_t i = 0; i < size(); ++i) {
            type::TypeCode typeCode = codes[i];
            if (type::isGCTraced(typeCode)) {
                const rtdata::Object *refA =
                    reinterpret_cast<const rtdata::Object *const *>(dataA)[i];
                const rtdata::Object *refB =
                    reinterpret_cast<const rtdata::Object *const *>(dataB)[i];
                if (refA == refB)
                    continue;
                if (!refA->equals(refB, structType->typeAt(i), true))
                    return false;
            } else {
                if (dataA[i] != dataB[i])
                    return false;
            }
        }

        return true;
    }

    virtual rtdata::Object *clone(
        camel::core::mm::IAllocator &allocator, const type::Type *type,
        bool deep = false) const override {
        GUARD(
            type != nullptr && type->code() == type::TypeCode::Struct,
            std::format(
                "Type must be StructType, and this call passed {}: the fields are read and "
                "written through the struct type's field types",
                type == nullptr
                    ? std::string("none")
                    : "code " + std::to_string(static_cast<int>(type->code()))));
        if (isView()) {
            return rtdata::materializeAggregate(viewData()->bytes, type, allocator);
        }
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        auto codes                         = structType->codes();

        Struct *newStruct = Struct::create(size(), allocator);
        const slot_t *src = data_;
        slot_t *dst       = newStruct->data_;

        for (size_t i = 0; i < size(); ++i) {
            if (type::isGCTraced(codes[i])) {
                const rtdata::Object *oriRef =
                    reinterpret_cast<const rtdata::Object *const *>(src)[i];
                rtdata::Object *newRef = rtdata::NullRef;

                if (oriRef) {
                    if (deep) {
                        // Recursively clone the referenced object.
                        newRef = oriRef->clone(allocator, structType->typeAt(i), true);
                    } else {
                        // Shallow copy: keep the original pointer.
                        newRef = const_cast<rtdata::Object *>(oriRef);
                    }
                }

                reinterpret_cast<rtdata::Object **>(dst)[i] = newRef;
                camel::core::mm::writeBarrier(newStruct, structType, newRef, structType->typeAt(i));
            } else {
                // Non-reference types: copy the slot data directly.
                dst[i] = src[i];
            }
        }

        return reinterpret_cast<rtdata::Object *>(newStruct);
    }

    virtual void print(std::ostream &os, const type::Type *type) const override {
        GUARD(
            type != nullptr && type->code() == type::TypeCode::Struct,
            std::format(
                "Type must be StructType, and this call passed {}: the fields are read and "
                "written through the struct type's field types",
                type == nullptr
                    ? std::string("none")
                    : "code " + std::to_string(static_cast<int>(type->code()))));
        if (size() == 0) {
            os << "{}";
            return;
        }

        const type::StructType *structType = static_cast<const type::StructType *>(type);

        os << "{ ";

        for (size_t i = 0; i < size(); ++i) {
            if (i > 0)
                os << ", ";

            os << structType->fieldName(i) << ": ";
            rtdata::printSlot(os, get<slot_t>(i), structType->typeAt(i));
        }

        os << " }";
    }

    virtual void onMoved() override {
        // No adjustment is needed.
    }

    virtual void
    updateRefs(const rtdata::Object::RefRelocator &relocate, const type::Type *type) override {
        if (isView()) {
            return; // pure data has no references
        }
        if (!type || type->code() != type::TypeCode::Struct)
            return;
        const type::StructType *structType = static_cast<const type::StructType *>(type);
        auto codes                         = structType->codes();
        rtdata::Object **refArr            = reinterpret_cast<rtdata::Object **>(data_);
        for (size_t i = 0; i < size(); ++i) {
            if (type::isGCTraced(codes[i])) {
                if (rtdata::Object *&ref = refArr[i]) {
                    const type::Type *slotType = structType->typeAt(i);
                    ref                        = relocate(
                        ref,
                        slotType,
                        rtdata::RefTraceInfo{
                            .owner     = this,
                            .ownerType = type,
                            .slotType  = slotType,
                            .ownerKind = "Struct",
                            .slotName  = structType->fieldName(i),
                            .slotIndex = i,
                        });
                }
            }
        }
    }

    /// Only the view ring (rtdata/view.cpp) passes flags; ordinary code uses create().
    explicit Struct(size_t fieldCount, uint32_t flags = 0)
        : size_(static_cast<uint32_t>(fieldCount) | flags) {}

  private:
    uint32_t size_;
    slot_t data_[]; // Flexible array member.
};

static_assert(
    alignof(Struct) == alignof(slot_t),
    "Struct alignment must be a slot's: aggregateSlots reads slots at the data offset.");