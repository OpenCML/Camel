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
 * Updated: Oct. 07, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "internal.h"

const std::unordered_map<InternalDiag, DiagInfo> &getInternalDiagInfoMap() {
    static const std::unordered_map<InternalDiag, DiagInfo> infoMap = {
        {
            InternalDiag::UnknownInternalError,
            {
                "UnknownInternalError",
                "An error has occurred: {0}.",
                "",
            },
        },
    };

    return infoMap;
}
