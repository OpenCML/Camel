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

#include "camel/core/error/diagnostics/messages/syntax.h"

namespace camel::core::error {

const std::unordered_map<SyntaxDiag, DiagInfo> &getSyntaxDiagInfoMap() {
    static const std::unordered_map<SyntaxDiag, DiagInfo> infoMap = {
        {
            SyntaxDiag::UnknownSyntaxError,
            {
                "UnknownSyntaxError",
                "Syntax error: {0}.",
                "",
            },
        },
        {
            SyntaxDiag::MissingClosingDelimiter,
            {
                "MissingClosingDelimiter",
                "Expected '{0}' before {2}, to close the '{1}'.",
                "Every '{1}' needs its '{0}'. The note shows the '{1}' that is still open here.",
            },
        },
        {
            SyntaxDiag::UnmatchedClosingDelimiter,
            {
                "UnmatchedClosingDelimiter",
                "Unexpected '{0}': no '{1}' is open here.",
                "Remove the '{0}', or add the '{1}' it was meant to close.",
            },
        },
        {
            SyntaxDiag::MissingThen,
            {
                "MissingThen",
                "Expected 'then' after the condition of the 'if', before {0}.",
                "A conditional is written: if <condition> then <value> else <value>. Each "
                "branch may be a block: if c then {{ ... }} else {{ ... }}.",
            },
        },
        {
            SyntaxDiag::ExtraSemicolon,
            {
                "ExtraSemicolon",
                "Unexpected ';': there is no statement between it and the ';' before it.",
                "Statements are separated by a line break or by one ';'. Remove the extra ';'.",
            },
        },
        {
            SyntaxDiag::AssignmentInCondition,
            {
                "AssignmentInCondition",
                "'=' assigns; it cannot be used in the condition of an 'if'.",
                "To compare, write '==': if x == 1 then ...",
            },
        },
        {
            SyntaxDiag::KeywordAsIdentifier,
            {
                "KeywordAsIdentifier",
                "'{0}' is a keyword and cannot be used as a name.",
                "Choose another name, for example '{0}_'.",
            },
        },
        {
            SyntaxDiag::MissingComma,
            {
                "MissingComma",
                "Expected {1} before {0}.",
                "The items of a list (parameters, arguments, elements) are separated by ','.",
            },
        },
        {
            SyntaxDiag::MissingInitializer,
            {
                "MissingInitializer",
                "The declaration of '{0}' has no value.",
                "A declaration binds a name to a value: let {0} = <value>, or with a type: let "
                "{0}: <type> = <value>.",
            },
        },
        {
            SyntaxDiag::MissingToken,
            {
                "MissingToken",
                "Expected {0} before {1}.",
                "",
            },
        },
        {
            SyntaxDiag::UnexpectedToken,
            {
                "UnexpectedToken",
                "Unexpected {0}{1}.",
                "",
            },
        },
        {
            SyntaxDiag::UnexpectedEndOfFile,
            {
                "UnexpectedEndOfFile",
                "The file ends in the middle of a construct{0}.",
                "Something opened earlier is not finished: check the last declaration of the "
                "file.",
            },
        },
    };

    return infoMap;
}

} // namespace camel::core::error
