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
 * Updated: Oct. 18, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "composed.h"

class TupleType : public ComposedType {
  private:
    std::vector<type_ptr_t> types_;

  public:
    TupleType();
    TupleType(const std::initializer_list<type_ptr_t> &types);
    TupleType(const std::vector<type_ptr_t> &types);

    std::string toString() const override;

    virtual std::string mangle() const override;

    std::optional<type_ptr_t> typeAt(struct_idx_t idx) const override;

    virtual bool resolved() const override;
    virtual void resolve(const type_vec_t &typeList) override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    virtual type_ptr_t clone() const override;

    void add(const type_ptr_t &type);
    void set(size_t index, const type_ptr_t &type);
    std::shared_ptr<TupleType> slice(size_t start, size_t end) const;

    size_t size() const;

    const std::vector<type_ptr_t> &types() const;

    CastSafety castSafetyTo(const Type &other) const override;
};
