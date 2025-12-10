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

#include "core/data/other/other.h"
#include "core/type/other.h"

class TensorType : public OtherType {
  private:
    std::vector<size_t> shape_;
    type_ptr_t element_type_;
    static std::unordered_map<std::string, std::string> staticMethods_;

  public:
    TensorType(const std::vector<size_t> &shape);
    TensorType(const type_ptr_t &elementType, const std::vector<size_t> &shape);
    virtual ~TensorType() noexcept = default;

    static TypeCode typeCode() {
        static TypeCode code =
            OtherTypeRegistry::registerType("Tensor", TypeFlag::Composite | TypeFlag::GC_Traced);
        return code;
    }

    static std::shared_ptr<TensorType>
    create(const type_ptr_t &elementType, const std::vector<size_t> &shape) {
        return std::make_shared<TensorType>(elementType, shape);
    }

    std::vector<size_t> shape() const;
    type_ptr_t dType() const;
    static void registerStaticMethod(const std::string &methodName, const std::string &operatorUri);
    static std::string getStaticMethodUri(const std::string &methodName);
    static bool hasStaticMethod(const std::string &methodName);

    static type_ptr_t Tensor(const std::vector<size_t> &shape);
    static type_ptr_t Default();

    virtual std::string toString() const override;
    virtual std::string mangle() const override;
    virtual type_ptr_t clone(bool deep = false) const override;
    virtual bool equals(const type_ptr_t &type) const override;
    virtual CastSafety castSafetyTo(const Type &other) const override;
    virtual bool assignable(const type_ptr_t &type) const override;
};
