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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/composite/composite.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>
#include <vector>

namespace camel::core::type {

// Forward declaration.
class StructType;

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

// Immutable StructType: type information is embedded directly in the object.
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
    virtual bool assignableFrom(Type *sourceType) const override;

  private:
    // Copy once from the precomputed layout and data.
    StructType(
        const StructTypeLayout &layout, const Type *const *types, const TypeCode *typeCodes,
        const size_t *nameOffsets, const char *fieldNamesData, const size_t *refs);

    // Helper: build from raw data (internal use).
    static StructType *fromFactoryData(
        const std::vector<std::pair<std::string, Type *>> &fields,
        const std::vector<std::string> &refs);

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
