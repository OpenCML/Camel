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
 * Updated: Dec. 17, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"

#include <optional>

class ArrayTypeLayout {
  public:
    explicit ArrayTypeLayout(TypeCode elemType, std::vector<size_t> refs)
        : elemType_(elemType), refs_(refs) {}

    TypeCode elemType() const noexcept { return elemType_; }
    const std::vector<size_t> &refs() const noexcept { return refs_; }

  private:
    TypeCode elemType_;
    std::vector<size_t> refs_;
};

class ArrayType : public CompositeType {
  private:
    type_ptr_t elemType_;
    std::vector<size_t> refs_;
    mutable std::shared_ptr<ArrayTypeLayout> layout_;

    void computeLayout() const;

  public:
    ArrayType(const type_ptr_t &elemType);
    ~ArrayType() noexcept override = default;

    static std::shared_ptr<ArrayType> create(const type_ptr_t &elemType = nullptr);

    void addRef(size_t index);
    void setRefs(const std::vector<size_t> &refs);
    type_ptr_t elemType() const;
    const ArrayTypeLayout &layout() const;

    virtual type_ptr_t resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual type_ptr_t clone(bool deep = false) const override;
    virtual bool equals(const type_ptr_t &type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(const type_ptr_t &type) const override;
};
