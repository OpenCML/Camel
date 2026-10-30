// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/rtdata/variant.h"

#include "camel/core/mm.h"
#include "camel/core/rtdata/equality.h"
#include "camel/core/type/enum.h"
#include "camel/utils/brpred.h"

#include <cstdio>
#include <cstdlib>
#include <format>
#include <ostream>
#include <string>

using namespace camel::core::type;

namespace camel::core::rtdata {

namespace {

const EnumType *enumTypeOf(const Type *type) {
    GUARD(
        type != nullptr,
        std::string("printing, comparing or tracing a Variant that carries no type, so there is "
                    "no field to name the value by"));
    const EnumType *enumType = asEnumType(type);
    GUARD(
        enumType != nullptr,
        std::string("reading a Variant as a tagged value, but the type it carries has type code ") +
            std::to_string(static_cast<int>(type->code())) + ", which is not an enum type");
    return enumType;
}

/// Whether the trace prints what it was handed for every value it relocates (CAMEL_TRACE_VARIANT=1).
bool traceWanted() {
    static const bool wanted = [] {
        const char *value = std::getenv("CAMEL_TRACE_VARIANT");
        return value != nullptr && value[0] != '\0' && value[0] != '0';
    }();
    return wanted;
}

/// Reports a `Variant` whose type the trace cannot use, and stops. It stays in a release build on
/// purpose: `ASSERT` compiles to nothing under `NDEBUG`, which is how this path used to take the
/// process down with no word at all (exit 139 in the training workload).
[[noreturn]] void reportUntraceableVariant(
    const void *value, const Type *type, size_t tag, size_t fields, const char *why) {
    std::fprintf(
        stderr,
        "[variant] the reference trace cannot use the type of a Variant: %s\n"
        "[variant]   value=%p type=%p tag=%zu fields=%zu\n"
        "[variant]   the type is the one the scan carried down from the slot the value was found\n"
        "[variant]   in, not the value's own: a type that reads as no enum means the slot does not\n"
        "[variant]   hold what it says; a pointer that cannot be read means the value outlived its\n"
        "[variant]   type. Neither is a shape the value was written with.\n",
        why,
        value,
        static_cast<const void *>(type),
        tag,
        fields);
    std::fflush(stderr);
    // Best effort, and it comes after the line above is already out: the type is readable enough to
    // report a code, and naming it is what tells a reader which slot the value was found in.
    if (type != nullptr) {
        std::fprintf(stderr, "[variant]   the type reads as: %s\n", type->toString().c_str());
        std::fflush(stderr);
    }
    std::abort();
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
    // How the trace knows a value's type: it is the type of the slot the value was found in, carried
    // down the scan with the object (mm/gen.cpp: TraceEntry, RefTraceInfo) -- not an id the value
    // holds, not its tag, and not a field of its header. So this is the one place a value can be
    // asked about a type that is no longer there, and the readings below say which of the three
    // shapes the break has. They are printed before the type is read, so a break the read cannot
    // survive still leaves them in the log; the check after it is in a release build on purpose
    // (ASSERT is not: that is how this path used to die with no word at all).
    if (UNLIKELY(traceWanted())) {
        std::fprintf(
            stderr,
            "[variant] trace value=%p type=%p tag=%zu fields=%zu\n",
            static_cast<const void *>(this),
            static_cast<const void *>(type),
            static_cast<size_t>(tag_),
            static_cast<size_t>(fieldCount_));
        std::fflush(stderr);
    }
    if (UNLIKELY(type == nullptr)) {
        reportUntraceableVariant(
            this,
            type,
            static_cast<size_t>(tag_),
            static_cast<size_t>(fieldCount_),
            "the scan recorded no type for it");
    }
    const EnumType *enumType = asEnumType(type);
    if (UNLIKELY(enumType == nullptr)) {
        reportUntraceableVariant(
            this,
            type,
            static_cast<size_t>(tag_),
            static_cast<size_t>(fieldCount_),
            std::format(
                "the type it was traced with has code {}, which is not an enum's",
                static_cast<int>(type->code()))
                .c_str());
    }
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