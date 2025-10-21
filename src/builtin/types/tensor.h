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
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/type/struct/struct.h"

class TensorType : public OtherType {
  private:
    std::vector<size_t> shape_;
    static std::unordered_map<std::string, std::string> staticMethods_;

  public:
    TensorType(const std::vector<size_t> &shape);

    std::vector<size_t> shape() const;
    
    type_ptr_t elementType() const;

    std::string toString() const override;

    virtual std::string mangle() const override;

    bool operator==(const Type &other) const override;
    bool operator!=(const Type &other) const override;

    virtual type_ptr_t clone() const override {
        ASSERT(false, "clone() not implemented");
        return nullptr;
    }

    CastSafety castSafetyTo(const Type &other) const override;
    static void registerStaticMethod(const std::string& methodName, const std::string& operatorUri);

    static std::string getStaticMethodUri(const std::string& methodName);
    static bool hasStaticMethod(const std::string& methodName);

    static type_ptr_t Tensor(const std::vector<size_t>& shape);
};