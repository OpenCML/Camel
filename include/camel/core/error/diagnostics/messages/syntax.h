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
 * Created: Sep. 11, 2025
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <unordered_map>

#include "../base.h"

namespace camel::core::error {

enum class SyntaxDiag : uint32_t {
    UnknownSyntaxError        = 0x000000,
    MissingClosingDelimiter   = 0x000001,
    UnmatchedClosingDelimiter = 0x000002,
    MissingThen               = 0x000003,
    ExtraSemicolon            = 0x000004,
    AssignmentInCondition     = 0x000005,
    KeywordAsIdentifier       = 0x000006,
    MissingComma              = 0x000007,
    MissingInitializer        = 0x000008,
    MissingToken              = 0x000009,
    UnexpectedToken           = 0x00000A,
    UnexpectedEndOfFile       = 0x00000B,
    ComparisonNeedsSpace      = 0x00000C,
    AngleCloseAfterSpace      = 0x00000D,
    LineStartsWithOperator    = 0x00000E,
    EmptyInterpolation        = 0x00000F,

    // Warnings (severity bits = Warn).
    ComparisonAcrossLines = 0x100001,
};

constexpr DiagType diagTypeOf(SyntaxDiag) { return DiagType::SyntaxDiag; }

const std::unordered_map<SyntaxDiag, DiagInfo> &getSyntaxDiagInfoMap();

} // namespace camel::core::error
