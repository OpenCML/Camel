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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 17, 2025
 * Updated: Jul. 17, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>

enum class LiteralType {
    String,
    FString,
    Integer,
    Real,
    Boolean,
    Null,
};

class Literal {
  public:
    Literal(LiteralType type, const std::string &data) : type_(type), data_(data) {}
    ~Literal() = default;
    const std::string toString() const { return data_; }

    LiteralType type() const { return type_; }
    const std::string &data() const { return data_; }

  private:
    LiteralType type_;
    std::string data_;
};