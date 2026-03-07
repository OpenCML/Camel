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
                "An error has occurred: {0}.",
                "",
            },
        },
    };

    return infoMap;
}

} // namespace camel::core::error
