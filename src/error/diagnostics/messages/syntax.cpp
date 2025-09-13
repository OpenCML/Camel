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
        {SyntaxDiag::UnknownSyntaxError,
         {"UnknownSyntaxError",
          "An unknown syntax error has occurred: {0}.",
          "Check the surrounding code for possible syntax issues."}},
    };
}