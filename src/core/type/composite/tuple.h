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
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"
#include "utils/assert.h"
#include "utils/log.h"

#include <optional>

class TupleTypeLayout {
  public:
    explicit TupleTypeLayout(std::vector<TypeCode> &&elemTypes, std::vector<size_t> &&refs)
        : elemTypes_(std::move(elemTypes)), refs_(std::move(refs)) {}
    ~TupleTypeLayout() {
        l.in("TupleTypeLayout")
            .debug("Destroying TupleTypeLayout with {} elements", elemTypes_.size());
    }

    size_t size() const noexcept { return elemTypes_.size(); }

    TypeCode typeAt(size_t index) const {
        ASSERT(index < elemTypes_.size(), "TupleTypeLayout: index out of range");
        return elemTypes_[index];
    }

    const std::vector<TypeCode> &elemTypes() const { return elemTypes_; }
    const std::vector<size_t> &refs() const { return refs_; }

  private:
    std::vector<TypeCode> elemTypes_;
    std::vector<size_t> refs_;
};

class TupleType : public CompositeType {
  private:
    std::vector<Type *> types_;
    mutable std::shared_ptr<TupleTypeLayout> layout_;

    void computeLayout() const;

  public:
    TupleType();
    TupleType(const std::initializer_list<Type *> &types);
    TupleType(const std::vector<Type *> &types);
    TupleType(std::vector<Type *> &&types);
    ~TupleType() noexcept override = default;

    static TupleType *create();
    static TupleType *create(const std::vector<Type *> &types);
    static TupleType *create(std::vector<Type *> &&types);

    void add(Type *type);
    void set(size_t index, Type *type);
    size_t size() const;
    const std::vector<Type *> &types() const;
    std::optional<Type *> typeAt(size_t idx) const;
    TupleType *slice(size_t start, size_t end) const;
    const TupleTypeLayout &layout() const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(Type *type) const override;
};
