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
 * Updated: Nov. 15, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"

#include <optional>

class TupleType : public CompositeType {
  private:
    std::vector<type_ptr_t> types_;

  public:
    TupleType();
    TupleType(const std::initializer_list<type_ptr_t> &types);
    TupleType(const std::vector<type_ptr_t> &types);
    TupleType(std::vector<type_ptr_t> &&types);
    ~TupleType() noexcept override = default;

    std::shared_ptr<TupleType> create(const std::vector<type_ptr_t> &types) const;
    std::shared_ptr<TupleType> create(std::vector<type_ptr_t> &&types) const;

    void add(const type_ptr_t &type);
    void set(size_t index, const type_ptr_t &type);
    size_t size() const;
    const std::vector<type_ptr_t> &types() const;
    std::shared_ptr<TupleType> slice(size_t start, size_t end) const;
    std::optional<type_ptr_t> typeAt(size_t idx) const;

    virtual type_ptr_t resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual type_ptr_t clone(bool deep = false) const override;
    virtual bool equals(const type_ptr_t &type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(const type_ptr_t &type) const override;
};
