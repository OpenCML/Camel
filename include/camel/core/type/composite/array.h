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

#include <vector>

namespace camel::core::type {

// Forward declaration.
class ArrayType;

// Factory for constructing ArrayType (may use STL containers).
class ArrayTypeFactory {
    friend class ArrayType;

  public:
    ArrayTypeFactory() = default;
    explicit ArrayTypeFactory(Type *elemType) : elemType_(elemType) {}

    void setElemType(Type *elemType) { elemType_ = elemType; }
    void addRef(size_t index) { refs_.push_back(index); }
    void setRefs(const std::vector<size_t> &refs) { refs_ = refs; }

    // Build an immutable ArrayType object.
    ArrayType *build();

  private:
    Type *elemType_ = nullptr;
    std::vector<size_t> refs_;
};

// Immutable ArrayType: type information is embedded directly in the object.
class ArrayType : public CompositeType {
    friend class ArrayTypeFactory;

  public:
    // Direct construction is forbidden; use the factory or create methods.
    ArrayType(const ArrayType &)            = delete;
    ArrayType &operator=(const ArrayType &) = delete;

    // Create a resolved ArrayType (no refs).
    static ArrayType *create(Type *elemType = nullptr);

    // Build from a factory.
    static ArrayType *fromFactory(ArrayTypeFactory &factory);

    // Build directly from data (internal use, e.g. clone).
    static ArrayType *fromData(Type *elemType, size_t refCount, const size_t *refs);

    Type *elemType() const { return elemType_; }
    TypeCode elemTypeCode() const { return elemTypeCode_; }
    size_t refCount() const { return refCount_; }
    const size_t *refs() const { return refs_; } // Return a pointer to the internal array.

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyFrom(Type *sourceType) const override;
    virtual bool assignableFrom(Type *sourceType) const override;

  private:
    // Private constructor: called by the factory or create.
    ArrayType(Type *elemType, size_t refCount, const size_t *refs);

    Type *elemType_;        // Element type pointer (used by the type system).
    TypeCode elemTypeCode_; // Element type code (used at runtime).
    size_t refCount_;       // Number of reference indices.
    size_t refs_[];         // Flexible array: list of reference indices.
};

} // namespace camel::core::type
