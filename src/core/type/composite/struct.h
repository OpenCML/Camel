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
#include <unordered_map>

class StructType : public CompositeType {
  private:
    std::vector<std::string> refIndices_;
    std::unordered_map<std::string, type_ptr_t> fields_;

  public:
    StructType();
    ~StructType() noexcept override = default;

    std::shared_ptr<StructType> create() const;

    size_t size() const;
    std::optional<type_ptr_t> typeOf(const std::string &idx) const;
    bool add(const std::string &name, const type_ptr_t &type);
    bool has(const std::string &name) const;
    type_ptr_t get(const std::string &name) const;

    type_ptr_t operator|(const StructType &other) const;
    type_ptr_t operator&(const StructType &other) const;

    virtual type_ptr_t resolve(const type_vec_t &typeList) const override;
    virtual bool resolved() const override;
    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual type_ptr_t clone(bool deep = false) const override;
    virtual bool equals(const type_ptr_t &type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(const type_ptr_t &type) const override;
};
