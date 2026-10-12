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

#include "camel/core/error/diagnostics/messages/lexical.h"

namespace camel::core::error {

const std::unordered_map<LexicalDiag, DiagInfo> &getLexicalDiagInfoMap() {
    static const std::unordered_map<LexicalDiag, DiagInfo> infoMap = {
        {
            LexicalDiag::IllegalCharacter,
            {
                "IllegalCharacter",
                "Illegal character '{0}'.",
                "No token starts with this character. Outside strings and comments a program "
                "consists of ASCII characters; a name is made of letters, digits and '_'.",
            },
        },
        {
            LexicalDiag::InvalidNumberFormat,
            {
                "InvalidNumberFormat",
                "Invalid number format: {0}.",
                "Check the syntax of the numeric literal.",
            },
        },
        {
            LexicalDiag::UnclosedStringLiteral,
            {
                "UnclosedStringLiteral",
                "This string literal is not closed.",
                "A string ends with the quote character it starts with ({0}). Add the closing "
                "quote; to put a quote character inside the string, write it with a backslash.",
            },
        },
        {
            LexicalDiag::UnterminatedComment,
            {
                "UnterminatedComment",
                "Unterminated comment: {0}.",
                "Close the comment with '*/'.",
            },
        },
        {
            LexicalDiag::InvalidEscapeSequence,
            {
                "InvalidEscapeSequence",
                "Invalid escape sequence in string: {0}.",
                "Check and correct the escape sequence.",
            },
        },
    };

    return infoMap;
}

} // namespace camel::core::error
