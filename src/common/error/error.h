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
 * Created: Aug. 1, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>
#include <regex>
#include <string>

#include "antlr4-runtime.h"

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