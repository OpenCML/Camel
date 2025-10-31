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
 * Updated: Oct. 31, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/data/other/other.h"
#include "core/type/other/other.h"

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
        static TypeCode code = OtherTypeRegistry::registerType("Tensor");
        return code;
    }

    static std::shared_ptr<TensorType>
    create(const type_ptr_t &elementType, const std::vector<size_t> &shape) {
        return std::make_shared<TensorType>(elementType, shape);
    }

    std::vector<size_t> shape() const;

    type_ptr_t elementType() const;

    std::string toString() const override;

    std::string mangle() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    virtual bool assignable(const type_ptr_t &type) const override {
        // 目标必须是 Tensor 类型
        if (type->code() != typeCode()) {
            return false;
        }
        // 暂时先不考虑元素类型和形状的兼容性问题
        return true;
    }

    type_ptr_t clone() const override {
        ASSERT(false, "clone() not implemented");
        return nullptr;
    }

    CastSafety castSafetyTo(const Type &other) const override;
    static void registerStaticMethod(const std::string &methodName, const std::string &operatorUri);

    static std::string getStaticMethodUri(const std::string &methodName);
    static bool hasStaticMethod(const std::string &methodName);

    static type_ptr_t Tensor(const std::vector<size_t> &shape);
    static type_ptr_t Default();
};
