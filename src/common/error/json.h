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
 * Created: May. 12, 2024
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "error.h"

class JSONErrorListener : public CamelErrorListener {
    std::string escapeString(const std::string &input) {
        std::string result;
        for (char c : input) {
            switch (c) {
                case '\\': result += "\\\\"; break;
                case '\"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result += c; break;
            }
        }
        return result;
    }

  public:
    JSONErrorListener(std::string filename, std::ostream &os) : CamelErrorListener(filename, os) {}

    virtual void syntaxError(antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line,
                             size_t charPositionInLine, const std::string &msg, std::exception_ptr e) override {
        hasError = true;
        os << "{"
           << "\"filename\": \"" << filename << "\", "
           << "\"line\": " << line << ", "
           << "\"column\": " << charPositionInLine << ", "
           << "\"message\": \"" << escapeString(msg).c_str() << "\""
           << "}" << std::endl;
    }
};
