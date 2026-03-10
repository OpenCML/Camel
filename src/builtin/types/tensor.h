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
 * Updated: Mar. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/data/other.h"
#include "camel/core/type/other.h"

namespace type = camel::core::type;

class TensorType : public type::OtherType {
  private:
    std::vector<size_t> shape_;
    type::Type *element_type_;
    static std::unordered_map<std::string, std::string> staticMethods_;

  public:
    TensorType(const std::vector<size_t> &shape);
    TensorType(type::Type *elementType, const std::vector<size_t> &shape);
    virtual ~TensorType() noexcept = default;

  protected:
    /** 供 cloneWithParams 使用：仅带 code 与 params，无 shape/element_type。 */
    TensorType(type::TypeCode code, size_t paramCount, type::Type **params);

  public:
    static type::TypeCode typeCode();

    static TensorType *create(type::Type *elementType, const std::vector<size_t> &shape);
    static TensorType *create(const std::vector<size_t> &shape);
    static TensorType *Dynamic(type::Type *elementType = nullptr);

    std::vector<size_t> shape() const;
    type::Type *dType() const;
    static void registerStaticMethod(const std::string &methodName, const std::string &operatorUri);
    static std::string getStaticMethodUri(const std::string &methodName);
    static bool hasStaticMethod(const std::string &methodName);

    static type::Type *Tensor(const std::vector<size_t> &shape);
    static type::Type *Default();

    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual type::Type *clone(bool deep = false) const override;
    virtual bool equals(type::Type *type) const override;
    virtual type::CastSafety castSafetyFrom(type::Type *sourceType) const override;
    virtual bool assignableFrom(type::Type *sourceType) const override;
    type::OtherType *cloneWithParams(std::span<type::Type *const> params) const override;
};
