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
 * Created: Aug. 01, 2024
 * Updated: Feb. 06, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>
#include <regex>
#include <string>

#include "antlr4-runtime/antlr4-runtime.h"

class CamelErrorListener : public antlr4::BaseErrorListener {
  protected:
    std::string filename;
    std::ostream &os;
    bool hasError = false;

  public:
    CamelErrorListener(std::string filename, std::ostream &os) : filename(filename), os(os) {}

    bool hasErrors() { return hasError; }

    virtual void syntaxError(antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line,
                             size_t charPositionInLine, const std::string &msg, std::exception_ptr e) override {
        hasError = true;
        os << "line " << line << ", column " << charPositionInLine << ": " << msg << std::endl;
    }
};
