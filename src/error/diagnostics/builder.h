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
 * Created: Sep. 06, 2025
 * Updated: Sep. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"
#include "range.h"

struct Diagnostic;
class Diagnostics;

// ---- DiagnosticBuilder ----
class DiagnosticBuilder {
  public:
    DiagnosticBuilder() = delete; // 不能默认构造，因为引用成员必须初始化
    DiagnosticBuilder(Diagnostics &diag) : diagnostics_(diag) {}

    // Position setting methods
    DiagnosticBuilder &at(const CharRange &range);
    DiagnosticBuilder &at(const TokenRange &range);
    DiagnosticBuilder &at(size_t token);
    DiagnosticBuilder &at(antlr4::Token *start, antlr4::Token *end);
    DiagnosticBuilder &at(antlr4::Token *token);

    // Message substitution + commit
    template <typename... Args> Diagnostic &commit(Args &&...args);

  private:
    friend class Diagnostics; // 允许 Diagnostics 访问私有成员

    Diagnostics &diagnostics_;
    SourceRange range_{};
    Severity severity_ = Severity::Error;

    DiagType type_{};
    uint32_t specific_{};
    std::string name_;
    std::string rawMessage_;
    std::string rawSuggestion_;
    std::string message_;
    std::string suggestion_;
};