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
 * Created: Jul. 13, 2025
 * Updated: Jul. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <exception>
#include <string>

class CamelBaseException : public std::exception {
  public:
    CamelBaseException(const std::string &msg) : message_(msg) {}
    CamelBaseException() {}
    virtual ~CamelBaseException() noexcept = default;

    virtual std::string what(bool json = false) const {
        if (json) {
            return "{\"type\": \"error\", \"message\": \"" + message_ + "\"}";
        } else {
            return "Error: " + message_;
        }
    }

    const char *what() const noexcept override { return message_.c_str(); }

  protected:
    std::string message_;

    std::string escapeJson(const std::string &input) const {
        std::ostringstream escaped;
        for (char c : input) {
            switch (c) {
            case '\"':
                escaped << "\\\"";
                break;
            case '\\':
                escaped << "\\\\";
                break;
            case '\b':
                escaped << "\\b";
                break;
            case '\f':
                escaped << "\\f";
                break;
            case '\n':
                escaped << "\\n";
                break;
            case '\r':
                escaped << "\\r";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20)
                    escaped << "\\u" << std::hex << (int)c;
                else
                    escaped << c;
            }
        }
        return escaped.str();
    }
};