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

#include "builder.h"

// ---- DiagnosticBuilder implementation ----
DiagnosticBuilder &DiagnosticBuilder::at(const TokenRange &range) {
    tokenRange_ = range;
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::at(size_t start, size_t end) {
    tokenRange_ = {start, end};
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::at(size_t token) {
    tokenRange_ = {token, token};
    return *this;
}