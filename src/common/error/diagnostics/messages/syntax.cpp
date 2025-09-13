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
 * Updated: Sep. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "syntax.h"

const std::unordered_map<SyntaxDiag, DiagInfo> getSyntaxDiagInfoMap() {
    return {
        {SyntaxDiag::UnmatchedParenthesis,
         {"UnmatchedParenthesis",
          "There is an unmatched parenthesis: {0}.",
          "Check for missing or extra parentheses."}},
        {SyntaxDiag::MissingSemicolon,
         {"MissingSemicolon",
          "A semicolon is missing at the end of the statement: {0}.",
          "Add a semicolon at the end of the statement."}},
        {SyntaxDiag::UnexpectedToken,
         {"UnexpectedToken",
          "An unexpected token was encountered: {0}.",
          "Check the syntax near the unexpected token."}},
    };
}