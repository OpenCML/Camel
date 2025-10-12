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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "lexical.h"

const std::unordered_map<LexicalDiag, DiagInfo> &getLexicalDiagInfoMap() {
    static const std::unordered_map<LexicalDiag, DiagInfo> infoMap = {
        {
            LexicalDiag::IllegalCharacter,
            {
                "LEX_ILLEGAL_CHAR",
                "Illegal character encountered: {0}",
                "Remove or replace the unsupported character",
            },
        },
        {
            LexicalDiag::InvalidNumberFormat,
            {
                "LEX_INVALID_NUMBER",
                "Invalid number format: {0}",
                "Check numeric literal syntax",
            },
        },
        {
            LexicalDiag::UnclosedStringLiteral,
            {
                "LEX_UNCLOSED_STRING",
                "Unclosed string literal: {0}",
                "Ensure all string literals are properly closed",
            },
        },
        {
            LexicalDiag::UnterminatedComment,
            {
                "LEX_UNTERMINATED_COMMENT",
                "Unterminated comment: {0}",
                "Ensure all comments are properly closed",
            },
        },
        {
            LexicalDiag::InvalidEscapeSequence,
            {
                "LEX_INVALID_ESCAPE",
                "Invalid escape sequence in string: {0}",
                "Check and correct the escape sequences",
            },
        },
    };

    return infoMap;
}
