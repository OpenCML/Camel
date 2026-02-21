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
 * Updated: Feb. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composite.h"

#include <unordered_set>

class UnionType : public CompositeType {
  private:
    std::unordered_set<Type *> types_;

    void insertUnion(const UnionType &other);

  public:
    UnionType();
    UnionType(Type *lhs, Type *rhs);
    UnionType(const std::initializer_list<Type *> &types);
    UnionType(const std::vector<Type *> &types);
    ~UnionType() noexcept override = default;

    static UnionType *create();
    static UnionType *create(Type *lhs, Type *rhs);
    static UnionType *create(const std::initializer_list<Type *> &types);
    static UnionType *create(const std::vector<Type *> &types);

    void add(Type *type);
    bool has(Type *type) const;

    virtual Type *resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyTo(Type *targetType) const override;
    virtual bool assignable(Type *type) const override;
};
