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

#include "camel/core/data/other.h"
#include "camel/core/type/other.h"

class TensorType : public OtherType {
  private:
    std::vector<size_t> shape_;
    Type *element_type_;
    static std::unordered_map<std::string, std::string> staticMethods_;

  public:
    TensorType(const std::vector<size_t> &shape);
    TensorType(Type *elementType, const std::vector<size_t> &shape);
    virtual ~TensorType() noexcept = default;

  protected:
    /** 供 cloneWithParams 使用：仅带 code 与 params，无 shape/element_type。 */
    TensorType(TypeCode code, size_t paramCount, Type **params);

  public:
    static TypeCode typeCode() {
        static TypeCode code =
            OtherTypeRegistry::registerType("Tensor", TypeFlag::Composite | TypeFlag::GC_Traced);
        return code;
    }

    static TensorType *create(Type *elementType, const std::vector<size_t> &shape);
    static TensorType *create(const std::vector<size_t> &shape);

    std::vector<size_t> shape() const;
    Type *dType() const;
    static void registerStaticMethod(const std::string &methodName, const std::string &operatorUri);
    static std::string getStaticMethodUri(const std::string &methodName);
    static bool hasStaticMethod(const std::string &methodName);

    static Type *Tensor(const std::vector<size_t> &shape);
    static Type *Default();

    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual Type *clone(bool deep = false) const override;
    virtual bool equals(Type *type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(Type *type) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;
};
