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
 * Created: Oct. 06, 2024
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/composite/composite.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

/*
 * Struct types (design principles 5.2).
 *
 * Two kinds share this class:
 *
 *  - An anonymous struct type is one written in the source (`{ x: int, y: int }`, the type of a
 *    struct literal). Its identity is its structure: the field names and types in the order
 *    written. Two anonymous struct types with the same fields in a different order are
 *    different types, and the order is part of the type.
 *  - A named struct type is declared by `type Point = { x: float, y: float }`. Its identity is
 *    the declaration and, for a generic declaration, the type arguments (`Pair<int>`): two
 *    declarations with the same fields are different types. Its fields are always in the order
 *    of the declaration. `StructDecl` is the declaration, one per `type` statement; the
 *    instance objects are what a use of the name denotes.
 *
 * The conversion rules live on StructType::assignableFrom and castSafetyFrom: an unnamed value
 * may enter a named type whose fields match by name (that is what makes a struct literal usable
 * at a parameter of a named type), while between two named types an explicit `as` is required.
 * An explicit conversion matches fields by name; where that would reorder the fields it is not
 * a no-op and the builder lowers it to a copy (see camel/compile).
 */
namespace camel::core::type {

// Forward declarations.
class StructType;
class StructDecl;

// Layout information: compute once to avoid duplication.
struct StructTypeLayout {
    size_t totalSize;
    size_t size;
    size_t refCount;
    size_t alignedTypesSize;
    size_t alignedTypeCodesSize;
    size_t alignedNameOffsetsSize;
    size_t alignedRefsSize;
    size_t fieldNamesDataSize;

    struct DataPtrs {
        Type **types;
        TypeCode *typeCodes;
        size_t *nameOffsets;
        size_t *refs;
        char *fieldNamesBase;
    };
    DataPtrs ptrs(uint8_t *data) const {
        size_t off = 0;
        Type **t   = reinterpret_cast<Type **>(data + off);
        off += alignedTypesSize;
        TypeCode *tc = reinterpret_cast<TypeCode *>(data + off);
        off += alignedTypeCodesSize;
        size_t *no = reinterpret_cast<size_t *>(data + off);
        off += alignedNameOffsetsSize;
        size_t *r = reinterpret_cast<size_t *>(data + off);
        off += alignedRefsSize;
        char *fn = reinterpret_cast<char *>(data + off);
        return {t, tc, no, r, fn};
    }
};
// Factory: collect fields and build Type and StructData once to avoid recomputing Type.
// Factory for constructing StructType (may use STL).
class StructTypeFactory {
    friend class StructType;

  public:
    StructTypeFactory() = default;

    bool add(const std::string &name, Type *type) {
        if (has(name)) {
            return false;
        }
        fields_.emplace_back(name, type);
        if (type && type->code() == TypeCode::Ref) {
            refs_.push_back(name);
        }
        return true;
    }

    bool has(const std::string &name) const {
        return std::any_of(fields_.begin(), fields_.end(), [&](const auto &p) {
            return p.first == name;
        });
    }

    // Build an immutable StructType object.
    StructType *build();

  private:
    std::vector<std::pair<std::string, Type *>> fields_;
    std::vector<std::string> refs_;
};

// One `type` declaration of a struct type. The declaration holds what the source wrote: the
// name, the parameters and the field types in terms of them. Instances are StructType objects
// (StructType::get); the declaration itself is never the type of a value.
class StructDecl {
  public:
    /// `name` is the name as written, qualified by its namespaces; `params` are the type
    /// parameters of a generic declaration (empty otherwise). `module` is the module that
    /// declares the type: an operator on a value of the type is looked for there as well
    /// (design principles 5.7), so the declaration keeps the module it belongs to.
    static StructDecl *create(
        const std::string &name, std::vector<std::string> params,
        std::string module = std::string());

    const std::string &name() const { return name_; }
    /// The name the declaring module is loaded under (`models`), empty when the declaration was
    /// made outside a module. Not part of the type's identity: the declaration is.
    const std::string &moduleName() const { return module_; }
    /// The last segment of the name (`Point` for `models::Point`), for messages that already
    /// name the namespace.
    const std::string &shortName() const { return shortName_; }
    /// A dense process-local identity, part of the mangled name of an instance.
    uint32_t id() const { return id_; }
    const std::vector<std::string> &params() const { return params_; }
    bool generic() const { return !params_.empty(); }

    /// The field types as written, in the order of the declaration, with the parameters still
    /// in them. Called once, when the declaration is read.
    void define(std::vector<std::pair<std::string, Type *>> fields);
    bool defined() const { return defined_; }
    const std::vector<std::pair<std::string, Type *>> &fields() const { return fields_; }

    StructDecl(const StructDecl &)            = delete;
    StructDecl &operator=(const StructDecl &) = delete;

  private:
    StructDecl(const std::string &name, std::vector<std::string> params, std::string module);

    std::string name_;
    std::string shortName_;
    std::string module_;
    std::vector<std::string> params_;
    std::vector<std::pair<std::string, Type *>> fields_;
    uint32_t id_;
    bool defined_ = false;
};

// Immutable StructType: type information is embedded directly in the object.
/// Whether `source` is an unnamed struct value whose fields are `target`'s fields, by name,
/// and whose field types can each be read as the target field's (in any written order). This
/// is the relation that lets an unnamed struct value enter a named struct type (design
/// principles 5.2): `target` must be named and `source` unnamed, and when the two orders
/// differ the conversion needs a copy (camel/compile/gir convertStructValue).
bool structFieldsMatchByName(const StructType *target, const StructType *source);

class StructType : public CompositeType {
    friend class StructTypeFactory;

  public:
    // Direct construction is forbidden; use the factory or create methods.
    StructType(const StructType &)            = delete;
    StructType &operator=(const StructType &) = delete;

    // Create a resolved StructType.
    static StructType *create();

    // Build from a factory.
    static StructType *fromFactory(StructTypeFactory &factory);

    /// The instance of the declaration `decl` with the type arguments `args`: `Point`, or
    /// `Pair<int>` for a generic declaration. Instances without type variables are interned by
    /// (declaration, arguments), exactly as enum instances are, so a use of the name always
    /// denotes the same type object. `args` must have one entry per parameter (none for a
    /// non-generic declaration).
    static StructType *get(StructDecl *decl, std::span<Type *const> args);

    /// The declaration of a named struct type, or nullptr for an anonymous one.
    StructDecl *decl() const { return decl_; }
    bool named() const { return decl_ != nullptr; }
    size_t typeArgCount() const { return typeArgs_.size(); }
    Type *typeArgAt(size_t index) const {
        ASSERT(index < typeArgs_.size(), "StructType: type argument index out of range");
        return typeArgs_[index];
    }
    std::span<Type *const> typeArgs() const { return std::span<Type *const>(typeArgs_); }

    size_t size() const { return size_; }
    Type *typeAt(size_t index) const {
        ASSERT(index < size_, "StructType: index out of range");
        return const_cast<Type *>(typesPtr_[index]);
    }
    TypeCode codeAt(size_t index) const {
        ASSERT(index < size_, "StructType: index out of range");
        return typeCodesPtr_[index];
    }
    std::span<Type *const> types() const { return std::span<Type *const>(typesPtr_, size_); }
    std::span<const TypeCode> codes() const {
        return std::span<const TypeCode>(typeCodesPtr_, size_);
    }
    std::string_view fieldName(size_t index) const {
        ASSERT(index < size_, "StructType: index out of range");
        return fieldNameAt(index);
    }
    std::optional<size_t> findField(std::string_view name) const;
    size_t refCount() const { return refCount_; }
    const size_t *refs() const { return refsPtr(); }

    Type *operator|(const StructType &other) const;
    Type *operator&(const StructType &other) const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override;
    virtual slot_t castSlotFrom(slot_t value, Type *sourceType) const override;
    virtual bool assignableFrom(Type *sourceType) const override;
    /// Widens every element type, so `var` bindings never hold refined aggregates (mutation
    /// through `var` parameters would otherwise break covariant assignability).
    virtual Type *widened() const override;

  private:
    // Copy once from the precomputed layout and data.
    StructType(
        const StructTypeLayout &layout, const Type *const *types, const TypeCode *typeCodes,
        const size_t *nameOffsets, const char *fieldNamesData, const size_t *refs,
        StructDecl *decl = nullptr, std::vector<Type *> typeArgs = {});

    // The struct type with these fields, interned (camel/core/type/intern.h): the same field
    // names and field type objects, in the same order, always give the same StructType.
    static StructType *fromFactoryData(
        const std::vector<std::pair<std::string, Type *>> &fields,
        const std::vector<std::string> &refs);
    // A new, unshared object; only fromFactoryData() and get() call it.
    static StructType *allocate(
        const std::vector<std::pair<std::string, Type *>> &fields,
        const std::vector<std::string> &refs, StructDecl *decl = nullptr,
        std::vector<Type *> typeArgs = {});

    /// The declaration of a named type, or nullptr.
    StructDecl *decl_ = nullptr;
    /// Its type arguments, in parameter order; empty for an anonymous or non-generic type.
    std::vector<Type *> typeArgs_;

    size_t size_;            // Field count.
    size_t refCount_;        // Number of reference indices.
    Type **typesPtr_;        // Pointer to the types array (computed at construction).
    TypeCode *typeCodesPtr_; // Pointer to the typeCodes array (computed at construction).
    size_t *nameOffsetsPtr_; // Pointer to the nameOffsets array (computed at construction).
    size_t *refsPtr_;        // Pointer to the refs array (computed at construction).
    const char
        *fieldNamesBasePtr_; // Pointer to the field-name string data (computed at construction).
    // Flexible array: stores types_[size_], typeCodes_[size_], nameOffsets_[size_],
    // refs_[refCount_], and the field-name string data. Layout: [StructType
    // base][size_][refCount_][typesPtr_][typeCodesPtr_][nameOffsetsPtr_]
    // [refsPtr_][fieldNamesBasePtr_][types_[size_]][typeCodes_[size_]][nameOffsets_[size_]]
    // [refs_[refCount_]][field-name string data]
    uint8_t data_[]; // Flexible array: stores all data.

    // Accessors (return the stored pointers directly).
    const Type **typesPtr() const { return const_cast<const Type **>(typesPtr_); }
    const TypeCode *typeCodesPtr() const { return typeCodesPtr_; }
    const size_t *nameOffsetsPtr() const { return nameOffsetsPtr_; }
    const size_t *refsPtr() const { return refsPtr_; }
    const char *fieldNamesBasePtr() const { return fieldNamesBasePtr_; }
    // Resolve a field name via its offset.
    std::string_view fieldNameAt(size_t index) const {
        size_t offset = nameOffsetsPtr_[index];
        if (index + 1 < size_) {
            // Subtract the current offset from the next field offset, then subtract 1 to remove the
            // null terminator.
            size_t length = nameOffsetsPtr_[index + 1] - offset - 1;
            return std::string_view(fieldNamesBasePtr_ + offset, length);
        } else {
            // Last field: from the offset to the end of the string.
            return std::string_view(fieldNamesBasePtr_ + offset);
        }
    }
};

} // namespace camel::core::type
