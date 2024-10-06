/**
 * Copyright (c) 2022 Beijing Jiaotong University
 * PhotLab is licensed under [Open Source License].
 * You can use this software according to the terms and conditions of the [Open
 * Source License]. You may obtain a copy of [Open Source License] at:
 * [https://open.source.license/]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the [Open Source License] for more details.
 *
 * Author: Zhenjie Wei
 * Created: May. 12, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "error.h"

class JSONErrorListener : public CamelErrorListener {
    std::string escapeString(const std::string &input) {
        std::string result;
        for (char c : input) {
            if (c == '"') {
                result += "\\\"";
            } else if (c == '\n') {
                result += "\\n";
            } else {
                result += c;
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