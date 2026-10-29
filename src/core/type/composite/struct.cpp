// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/composite/struct.h"
#include "camel/core/mm.h"
#include "camel/core/mm/alloc/allocator.h"
#include "camel/core/symbol.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/storage.h"
#include "camel/core/type/text.h"
#include "camel/core/type/typevar.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <sstream>
#include <unordered_map>

namespace mm = camel::core::mm;
using namespace std;
using namespace camel::core::type;

namespace {

std::atomic<uint32_t> structDeclCounter{1};

} // namespace

StructDecl::StructDecl(
    const std::string &name, std::vector<std::string> params, std::string module)
    : name_(name), module_(std::move(module)), params_(std::move(params)),
      id_(structDeclCounter.fetch_add(1)) {
    const size_t at = name_.rfind("::");
    shortName_      = at == std::string::npos ? name_ : name_.substr(at + 2);
}

StructDecl *StructDecl::create(
    const std::string &name, std::vector<std::string> params, std::string module) {
    return new StructDecl(name, std::move(params), std::move(module));
}

void StructDecl::define(std::vector<std::pair<std::string, Type *>> fields) {
    ASSERT(!defined_, "A struct declaration is defined once.");
    fields_  = std::move(fields);
    defined_ = true;
}

namespace {

StructTypeLayout computeLayout(
    size_t size, size_t refCount, size_t refCapacity, size_t fieldNamesDataSize) {
    size_t typesSize       = size * sizeof(Type *);
    size_t typeCodesSize   = size * sizeof(TypeCode);
    size_t nameOffsetsSize = size * sizeof(size_t);
    size_t refsSize        = refCapacity * sizeof(size_t);
    size_t aTypes          = mm::alignUp(typesSize, alignof(TypeCode));
    size_t aTypeCodes      = mm::alignUp(typeCodesSize, alignof(size_t));
    size_t aNameOffsets    = mm::alignUp(nameOffsetsSize, alignof(size_t));
    size_t aRefs           = mm::alignUp(refsSize, alignof(char));
    size_t totalSize =
        sizeof(StructType) + aTypes + aTypeCodes + aNameOffsets + aRefs + fieldNamesDataSize;
    return StructTypeLayout{
        .totalSize              = totalSize,
        .size                   = size,
        .refCount               = refCount,
        .refCapacity            = refCapacity,
        .alignedTypesSize       = aTypes,
        .alignedTypeCodesSize   = aTypeCodes,
        .alignedNameOffsetsSize = aNameOffsets,
        .alignedRefsSize        = aRefs,
        .fieldNamesDataSize     = fieldNamesDataSize,
    };
}

} // namespace

StructType *StructTypeFactory::build() { return StructType::fromFactory(*this); }

namespace {

/// The intern key of a named instance: its declaration and the **canonical** ids of its type
/// arguments. The declaration decides which type it is (design principles 5.2), and a named type
/// compares its arguments with `equals` (text.cpp: sameType), so the names in them are erased
/// here exactly as they are there: the declaration's own parameter object and a use's argument
/// object are one entry, which is how a forward declaration and its definition share it.
std::vector<uintptr_t> declaredKey(const StructDecl *decl, std::span<Type *const> args) {
    std::vector<uintptr_t> key;
    key.reserve(1 + args.size());
    key.push_back(reinterpret_cast<uintptr_t>(decl));
    for (Type *arg : args) {
        key.push_back(typeKeyWord(TypeTable::instance().canonical(arg)));
    }
    return key;
}

std::vector<std::string> refsOf(const std::vector<std::pair<std::string, Type *>> &fields) {
    std::vector<std::string> refs;
    for (const auto &[name, type] : fields) {
        if (type != nullptr && type->code() == TypeCode::Ref) {
            refs.push_back(name);
        }
    }
    return refs;
}

} // namespace

StructType *StructType::get(StructDecl *decl, std::span<Type *const> args) {
    ASSERT(decl != nullptr, "StructType::get needs a declaration.");
    ASSERT(
        args.size() == decl->params().size(),
        "StructType::get: one type argument per parameter of the declaration.");
    // The instance of a declaration is one table entry, keyed by the declaration and the canonical
    // ids of the arguments. A declaration that names itself through an array is published while
    // its own fields are still being read (declare()/completeFields()): that entry is answered
    // here too, before the "defined" check below, because it is registered before its fields are
    // known and its key does not depend on them.
    const std::vector<uintptr_t> key = declaredKey(decl, args);
    if (Type *known = TypeTable::instance().find(TypeTable::Shape::DeclaredStruct, key)) {
        return static_cast<StructType *>(known);
    }
    ASSERT(decl->defined(), "A struct type is instantiated after its fields are read.");
    std::vector<std::pair<std::string, Type *>> fields;
    fields.reserve(decl->fields().size());
    if (decl->generic()) {
        type_bindings_t bindings;
        bindings.reserve(decl->params().size());
        for (size_t i = 0; i < decl->params().size(); ++i) {
            bindings.emplace_back(decl->params()[i], args[i]);
        }
        for (const auto &[name, fieldType] : decl->fields()) {
            fields.emplace_back(name, substituteTypeVars(fieldType, bindings));
        }
    } else {
        fields = decl->fields();
    }
    std::vector<std::string> refs = refsOf(fields);
    std::vector<Type *> typeArgs(args.begin(), args.end());
    return static_cast<StructType *>(TypeTable::instance().intern(
        TypeTable::Shape::DeclaredStruct, key,
        [&]() -> Type * { return allocate(fields, refs, decl, std::move(typeArgs)); }));
}

StructType::StructType(
    const StructTypeLayout &layout, const Type *const *types, const TypeCode *typeCodes,
    const size_t *nameOffsets, const char *fieldNamesData, const size_t *refs, StructDecl *decl,
    std::vector<Type *> typeArgs)
    : CompositeType(TypeCode::Struct), decl_(decl), typeArgs_(std::move(typeArgs)),
      size_(layout.size), refCount_(layout.refCount) {
    auto p             = layout.ptrs(data_);
    typesPtr_          = p.types;
    typeCodesPtr_      = p.typeCodes;
    nameOffsetsPtr_    = p.nameOffsets;
    refsPtr_           = p.refs;
    fieldNamesBasePtr_ = p.fieldNamesBase;
    for (size_t i = 0; i < size_; ++i) {
        p.types[i]       = const_cast<Type *>(types[i]);
        p.typeCodes[i]   = typeCodes[i];
        p.nameOffsets[i] = nameOffsets[i];
    }
    for (size_t i = 0; i < refCount_; ++i) {
        p.refs[i] = refs[i];
    }
    if (layout.fieldNamesDataSize > 0 && fieldNamesData) {
        std::memcpy(p.fieldNamesBase, fieldNamesData, layout.fieldNamesDataSize);
    }
}

StructType *StructType::create() { return fromFactoryData({}, {}); }

StructType *StructType::fromFactory(StructTypeFactory &factory) {
    return fromFactoryData(factory.fields_, factory.refs_);
}

StructType *StructType::fromFactoryData(
    const std::vector<std::pair<std::string, Type *>> &fields,
    const std::vector<std::string> &refs) {
    // One object per sequence of (field name, field type id); see intern.h. `refs` names
    // the fields whose type is Ref, which the fields already determine. A field type that is
    // itself a named type leaves the key as written, and the canonical id points at the struct of
    // the same shape whose field types are name-erased: that is what `equals` compares.
    std::vector<uintptr_t> key;
    std::vector<uintptr_t> plain;
    std::vector<std::pair<std::string, Type *>> plainFields;
    key.reserve(fields.size() * 2);
    plain.reserve(fields.size() * 2);
    plainFields.reserve(fields.size());
    for (const auto &[name, type] : fields) {
        Type *canonical = TypeTable::instance().canonical(type);
        key.push_back(camel::core::internSymbol(name));
        key.push_back(typeKeyWord(type));
        plain.push_back(camel::core::internSymbol(name));
        plain.push_back(typeKeyWord(canonical));
        plainFields.emplace_back(name, canonical);
    }
    Type *interned = TypeTable::instance().intern(
        TypeTable::Shape::Struct, key, [&]() -> Type * { return allocate(fields, refs); },
        TypeTable::Canonical{plain, [&]() -> Type * { return allocate(plainFields, refs); }});
    return static_cast<StructType *>(interned);
}

StructType *StructType::allocate(
    const std::vector<std::pair<std::string, Type *>> &fields,
    const std::vector<std::string> &refs, StructDecl *decl, std::vector<Type *> typeArgs,
    size_t refCapacity) {
    size_t size     = fields.size();
    size_t refCount = refs.size();
    // A declaration being read reserves one refs slot per field: the field types, and so which
    // fields are Ref, are not known yet. A completed type needs exactly refCount slots.
    if (refCapacity < refCount) {
        refCapacity = refCount;
    }

    std::vector<Type *> types;
    std::vector<TypeCode> typeCodes;
    std::vector<size_t> nameOffsets;
    std::vector<size_t> refIndices;
    std::string fieldNamesData;

    types.reserve(size);
    typeCodes.reserve(size);
    nameOffsets.reserve(size);

    size_t currentOffset = 0;
    for (size_t i = 0; i < size; ++i) {
        const auto &[name, type] = fields[i];
        types.push_back(type);
        // A field of a declaration that is still being read has no type yet; its code is filled
        // in by completeFields().
        typeCodes.push_back(type ? type->code() : TypeCode::Void);
        nameOffsets.push_back(currentOffset);
        fieldNamesData += name;
        fieldNamesData += '\0';
        currentOffset += name.length() + 1;
    }

    for (const auto &refName : refs) {
        auto it = std::find_if(fields.begin(), fields.end(), [&](const auto &p) {
            return p.first == refName;
        });
        ASSERT(it != fields.end(), "Ref name not found in fields");
        refIndices.push_back(std::distance(fields.begin(), it));
    }

    StructTypeLayout layout = computeLayout(size, refCount, refCapacity, fieldNamesData.size());
    EXEC_WHEN_DEBUG(CAMEL_LOG_DEBUG_S(
        "StructType",
        "Allocating StructType: size={}, refCount={}, totalSize: {} bytes",
        size,
        refCount,
        layout.totalSize));
    void *mem = allocTypeStorage<StructType>(TypeStorageKind::Struct, layout.totalSize);
    return new (mem) StructType(
        layout,
        types.data(),
        typeCodes.data(),
        nameOffsets.data(),
        fieldNamesData.data(),
        refIndices.data(),
        decl,
        std::move(typeArgs));
}

StructType *StructType::declare(
    StructDecl *decl, std::span<Type *const> args, const std::vector<std::string> &fieldNames) {
    ASSERT(decl != nullptr, "StructType::declare needs a declaration.");
    ASSERT(!decl->defined(), "StructType::declare is called before the fields are read.");
    ASSERT(
        args.size() == decl->params().size(),
        "StructType::declare: one type argument per parameter of the declaration.");
    std::vector<std::pair<std::string, Type *>> fields;
    fields.reserve(fieldNames.size());
    for (const std::string &name : fieldNames) {
        fields.emplace_back(name, nullptr);
    }
    std::vector<Type *> typeArgs(args.begin(), args.end());
    // The entry is registered now, under a key that does not depend on the fields, so that a
    // declaration that names itself through an array resolves to this object while its fields are
    // still being read. The object is therefore mutable until completeFields() runs, which the
    // table allows for this one shape: the key is the declaration, not the fields.
    const std::vector<uintptr_t> key = declaredKey(decl, args);
    return static_cast<StructType *>(TypeTable::instance().intern(
        TypeTable::Shape::DeclaredStruct, key, [&]() -> Type * {
            return allocate(fields, {}, decl, std::move(typeArgs), fieldNames.size());
        }));
}

void StructType::completeFields(const std::vector<std::pair<std::string, Type *>> &fields) {
    ASSERT(decl_ != nullptr, "Only a declared type is completed.");
    ASSERT(fields.size() == size_, "A struct instance has one field per declared field.");
    size_t refCount = 0;
    for (size_t i = 0; i < size_; ++i) {
        ASSERT(fields[i].first == fieldName(i), "The fields keep the order of the declaration.");
        Type *type = fields[i].second;
        ASSERT(type != nullptr, "A completed struct type has a type for every field.");
        typesPtr_[i]     = type;
        typeCodesPtr_[i] = type->code();
        if (type->code() == TypeCode::Ref) {
            // declare() reserves one refs slot per field, so this cannot overflow.
            ASSERT(refCount < size_, "Too many Ref fields for the reserved refs array.");
            refsPtr_[refCount++] = i;
        }
    }
    refCount_ = refCount;
}

std::optional<size_t> StructType::findField(std::string_view name) const {
    for (size_t i = 0; i < size_; ++i) {
        if (fieldName(i) == name) {
            return i;
        }
    }
    return std::nullopt;
}

Type *StructType::operator|(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "StructType::operator| requires resolved operands");

    StructTypeFactory factory;
    // Copy lhs fields.
    for (size_t i = 0; i < size_; ++i) {
        factory.add(std::string(fieldName(i)), typeAt(i));
    }
    // Add fields that exist only in rhs.
    for (size_t i = 0; i < other.size_; ++i) {
        std::string name(other.fieldName(i));
        if (!factory.has(name)) {
            factory.add(name, other.typeAt(i));
        }
    }
    return factory.build();
}

Type *StructType::operator&(const StructType &other) const {
    ASSERT(resolved() && other.resolved(), "StructType::operator& requires resolved operands");

    StructTypeFactory factory;
    for (size_t i = 0; i < size_; ++i) {
        std::string name(fieldName(i));
        auto optIdx = other.findField(name);
        if (optIdx.has_value()) {
            factory.add(name, typeAt(i));
        }
    }
    return factory.build();
}

Type *StructType::resolve(const type_vec_t &typeList) const {
    ASSERT(typeList.size() == refCount_, "StructType::resolve: typeList size mismatch");
    ASSERT(!resolved(), "StructType is already resolved");

    StructTypeFactory factory;
    // Copy all fields.
    for (size_t i = 0; i < size_; ++i) {
        factory.add(std::string(fieldName(i)), typeAt(i));
    }
    // Resolve refs.
    const size_t *refIndices = refs();
    for (size_t i = 0; i < refCount_; ++i) {
        size_t refIdx = refIndices[i];
        // Update the corresponding field type.
        // Note: the factory would need to support updating existing fields.
        // We rebuild here because the factory does not support updates.
        std::string refName(fieldName(refIdx));
        // In practice we need to rebuild the entire factory.
    }
    // Simplification: rebuild the entire struct.
    StructTypeFactory newFactory;
    size_t typeListIdx = 0;
    for (size_t i = 0; i < size_; ++i) {
        Type *fieldType = typeAt(i);
        if (fieldType->code() == TypeCode::Ref) {
            if (typeListIdx < typeList.size()) {
                newFactory.add(std::string(fieldName(i)), typeList[typeListIdx++]);
            }
        } else {
            newFactory.add(std::string(fieldName(i)), fieldType);
        }
    }
    return newFactory.build();
}

bool StructType::resolved() const { return refCount_ == 0; }

string StructType::toString() const { return typeToString(this); }

string StructType::mangle() const { return typeMangle(this); }

Type *StructType::clone(bool deep /* = false */) const {
    if (named()) {
        // A named type is its declaration: a copy of it is not another type.
        return const_cast<StructType *>(this);
    }
    StructTypeFactory factory;
    for (size_t i = 0; i < size_; ++i) {
        Type *fieldType = typeAt(i);
        factory.add(
            std::string(fieldName(i)),
            deep && fieldType ? fieldType->clone(true) : fieldType);
    }
    return factory.build();
}

bool StructType::equals(Type *type) const { return typeEquals(this, type); }

StructRelation camel::core::type::relateStructs(const StructType *target, const StructType *source) {
    if (target == nullptr || source == nullptr) {
        return StructRelation::None;
    }
    if (target == source || target->equals(const_cast<StructType *>(source))) {
        return StructRelation::SameLayout;
    }
    // A struct type still holding the `Ref` placeholder of a field is not one a value can have
    // (design principles 5.1: every value has a determined type, and no type is undetermined
    // after compilation). `resolved()` names that state, and the fields are filled from the
    // computed elements of a literal. No field of it can be related to the other type's fields,
    // so there is no relation to state -- and comparing the placeholder with a field type is a
    // question the primitive comparison cannot answer (it asserts on it).
    if (!target->resolved() || !source->resolved()) {
        return StructRelation::None;
    }
    if (target->size() != source->size()) {
        return StructRelation::None;
    }
    bool sameLayout = true;
    for (size_t i = 0; i < target->size(); ++i) {
        const auto from = source->findField(target->fieldName(i));
        if (!from.has_value()) {
            return StructRelation::None;
        }
        if (*from != i) {
            sameLayout = false;
        }
        Type *targetField = target->typeAt(i);
        Type *sourceField = source->typeAt(*from);
        if (targetField == nullptr || sourceField == nullptr) {
            return StructRelation::None;
        }
        // A field of a refined module type (`f32[..]` where `Tensor` is declared) keeps its one
        // slot, so it is readable as the target field's type without a move.
        if (targetField->assignableFrom(sourceField)) {
            continue;
        }
        // The empty array literal takes the element type of the field it stands in, which is a copy
        // of the field (design principles 5.1).
        if (targetField->code() == TypeCode::Array && isUntypedArrayType(sourceField)) {
            sameLayout = false;
            continue;
        }
        // A field that is itself a struct is related the same way, by name.
        if (targetField->code() == TypeCode::Struct && sourceField->code() == TypeCode::Struct) {
            switch (relateStructs(
                static_cast<const StructType *>(targetField),
                static_cast<const StructType *>(sourceField))) {
            case StructRelation::SameLayout:
                continue;
            case StructRelation::ByName:
                sameLayout = false;
                continue;
            case StructRelation::None:
                return StructRelation::None;
            }
        }
        return StructRelation::None;
    }
    return sameLayout ? StructRelation::SameLayout : StructRelation::ByName;
}

bool camel::core::type::structFieldsMatchByName(
    const StructType *target, const StructType *source) {
    if (target == nullptr || source == nullptr || !target->named() || source->named()) {
        return false;
    }
    return relateStructs(target, source) != StructRelation::None;
}

// Converting a value to its own type is legal and does nothing (design principles 5.1). An
// unnamed struct value whose fields are those of a named type in the declaration's order is
// that type without moving anything, so the conversion is safe too; the CAST keeps the type
// the value is used as. Between two named types the same holds when the fields are in the
// same order (the `as` that design principles 5.2 requires is written by the programmer, and
// changes the type without moving anything); when they are not, the conversion is a copy, and
// is not safe (it is not castSlotFrom at all).
CastSafety StructType::castSafetyFrom(Type *sourceType) const {
    if (sourceType == nullptr || sourceType->code() != TypeCode::Struct) {
        return CastSafety::Forbidden;
    }
    if (equals(sourceType)) {
        return CastSafety::Safe;
    }
    // Only a conversion that moves nothing is Safe; a copy in another field order is made by the
    // builder (convertStructValue), which asks relateStructs as well.
    const auto *source = static_cast<const StructType *>(sourceType);
    return relateStructs(this, source) == StructRelation::SameLayout ? CastSafety::Safe
                                                                     : CastSafety::Forbidden;
}

// Every conversion castSafetyFrom admits keeps the value.
slot_t StructType::castSlotFrom(slot_t value, Type *sourceType) const {
    ASSERT(
        castSafetyFrom(sourceType) == CastSafety::Safe,
        "castSlotFrom only allowed when castSafetyFrom returns Safe");
    return value;
}

// A named type is its declaration: only the same declaration with the same type arguments is
// the same type (design principles 5.2). An unnamed value may be used as a named type whose
// fields are its fields, by name, in the declaration's order; a struct literal at a position
// of a named type is built there in that order, so its written order is free (the builder
// converts it before this check). An unnamed type accepts any struct whose fields are its
// fields in its own (written) order, with covariant field types (e.g. { w: float32[..] }
// where { w: Tensor } is expected).
bool StructType::assignableFrom(Type *sourceType) const {
    if (sourceType == nullptr || sourceType->code() != TypeCode::Struct) {
        return false;
    }
    if (this == sourceType || equals(sourceType)) {
        return true;
    }
    const auto *other = static_cast<const StructType *>(sourceType);
    if (named() && other->named()) {
        return false;
    }
    return relateStructs(this, other) == StructRelation::SameLayout;
}

// Least common type at a branch join: two anonymous struct types with the same fields in the same
// written order whose field types have a common type each are the struct of those; the same
// named type with the same arguments is itself. A named type and an anonymous one, two named
// types and other field names have none.
Type *StructType::unify(Type *other) const {
    if (other == nullptr || other->code() != TypeCode::Struct) {
        return nullptr;
    }
    if (equals(other)) {
        return const_cast<StructType *>(this);
    }
    const auto *rhs = static_cast<const StructType *>(other);
    if (named() || rhs->named() || size() != rhs->size()) {
        return nullptr;
    }
    StructTypeFactory factory;
    for (size_t i = 0; i < size(); ++i) {
        if (fieldName(i) != rhs->fieldName(i) || typeAt(i) == nullptr || rhs->typeAt(i) == nullptr) {
            return nullptr;
        }
        Type *field = typeAt(i)->unify(rhs->typeAt(i));
        if (field == nullptr) {
            return nullptr;
        }
        factory.add(std::string(fieldName(i)), field);
    }
    return factory.build();
}

Type *StructType::widened() const {
    if (named()) {
        // The fields of a named type are the declared ones; nothing refines them.
        return const_cast<StructType *>(this);
    }
    StructTypeFactory factory;
    bool changed = false;
    for (size_t i = 0; i < size_; ++i) {
        Type *fieldType = typeAt(i);
        Type *widened   = fieldType ? fieldType->widened() : fieldType;
        changed         = changed || widened != fieldType;
        factory.add(std::string(fieldName(i)), widened);
    }
    return changed ? factory.build() : const_cast<StructType *>(this);
}
