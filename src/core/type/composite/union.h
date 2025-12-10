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
 * Updated: Dec. 10, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"

#include <unordered_set>

class UnionType : public CompositeType {
  private:
    std::unordered_set<type_ptr_t> types_;

    void insertUnion(const UnionType &other);

  public:
    UnionType();
    UnionType(const type_ptr_t &lhs, const type_ptr_t &rhs);
    UnionType(const std::initializer_list<type_ptr_t> &types);
    UnionType(const std::vector<type_ptr_t> &types);
    ~UnionType() noexcept override = default;

    static std::shared_ptr<UnionType> create(const type_ptr_t &lhs, const type_ptr_t &rhs);
    static std::shared_ptr<UnionType> create(const std::initializer_list<type_ptr_t> &types);
    static std::shared_ptr<UnionType> create(const std::vector<type_ptr_t> &types);

    void add(const type_ptr_t &type);
    bool has(const type_ptr_t &type) const;

    virtual type_ptr_t resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::optional<type_ptr_t> typeAt(size_t idx) const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual type_ptr_t clone(bool deep = false) const override;
    virtual bool equals(const type_ptr_t &type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(const type_ptr_t &type) const override;
};
