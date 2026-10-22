// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/rtdata/variant.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/equality.h"
#include "camel/core/type/enum.h"

#include <ostream>

using namespace camel::core::type;

namespace camel::core::rtdata {

namespace {

const EnumType *enumTypeOf(const Type *type) {
    ASSERT(type != nullptr, "A variant value is printed, compared or traced by its type.");
    const EnumType *enumType = asEnumType(type);
    ASSERT(enumType != nullptr, "A variant value must have an enum type.");
    return enumType;
}

} // namespace

bool Variant::equals(const Object *other, const Type *type, bool deep) const {
    (void)deep;
    if (!isOfSameCls(this, other)) {
        return false;
    }
    const auto *that = static_cast<const Variant *>(other);
    if (tag_ != that->tag_ || fieldCount_ != that->fieldCount_) {
        return false;
    }
    const EnumVariant &variant = enumTypeOf(type)->variant(tag_);
    for (size_t i = 0; i < fieldCount_; ++i) {
        if (!valuesEqualByContent(data_[i], that->data_[i], variant.fieldTypes[i])) {
            return false;
        }
    }
    return true;
}

Object *Variant::clone(mm::IAllocator &allocator, const Type *type, bool deep) const {
    const EnumVariant &variant = enumTypeOf(type)->variant(tag_);
    Variant *copy              = Variant::create(tag_, fieldCount_, allocator);
    for (size_t i = 0; i < fieldCount_; ++i) {
        const Type *fieldType = variant.fieldTypes[i];
        if (!fieldType->isGCTraced()) {
            copy->data_[i] = data_[i];
            continue;
        }
        const auto *ref = fromSlot<const Object *>(data_[i]);
        if (ref != nullptr && deep) {
            copy->data_[i] = toSlot(ref->clone(allocator, fieldType, true));
        } else {
            copy->data_[i] = data_[i];
        }
    }
    return copy;
}

void Variant::print(std::ostream &os, const Type *type) const {
    const EnumType *enumType   = enumTypeOf(type);
    const EnumVariant &variant = enumType->variant(tag_);
    os << enumType->decl()->shortName() << "::" << variant.name;
    switch (variant.shape) {
    case VariantShape::Unit:
        return;
    case VariantShape::Tuple:
        os << "(";
        for (size_t i = 0; i < fieldCount_; ++i) {
            if (i > 0) {
                os << ", ";
            }
            printSlot(os, data_[i], variant.fieldTypes[i]);
        }
        os << ")";
        return;
    case VariantShape::Struct:
        os << " { ";
        for (size_t i = 0; i < fieldCount_; ++i) {
            if (i > 0) {
                os << ", ";
            }
            os << variant.fieldNames[i] << ": ";
            printSlot(os, data_[i], variant.fieldTypes[i]);
        }
        os << " }";
        return;
    }
}

void Variant::updateRefs(const RefRelocator &relocate, const Type *type) {
    const EnumType *enumType = enumTypeOf(type);
    if (tag_ >= enumType->variantCount()) {
        return;
    }
    const EnumVariant &variant = enumType->variant(tag_);
    if (variant.shape == VariantShape::Unit) {
        return;
    }
    for (size_t i = 0; i < fieldCount_ && i < variant.fieldCount(); ++i) {
        const Type *fieldType = variant.fieldTypes[i];
        if (!fieldType->isGCTraced()) {
            continue;
        }
        Object *&ref = reinterpret_cast<Object *&>(data_[i]);
        if (ref == nullptr) {
            continue;
        }
        ref = relocate(
            ref,
            fieldType,
            RefTraceInfo{
                .owner     = this,
                .ownerType = type,
                .slotType  = fieldType,
                .ownerKind = "Variant",
                .slotName  = variant.shape == VariantShape::Struct ? variant.fieldNames[i]
                                                                   : std::string_view{},
                .slotIndex = i,
            });
    }
}

} // namespace camel::core::rtdata