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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/error/diagnostics/builder.h"

namespace camel::core::error {

// ---- DiagnosticBuilder implementation ----
DiagnosticBuilder &DiagnosticBuilder::at(const CharRange &range) {
    range_ = range;
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::at(const TokenRange &range) {
    range_ = range;
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::atSpan(camel::source::span_id_t span) {
    range_ = span;
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::atOrigin(camel::source::origin_id_t origin) {
    range_ = origin;
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::at(size_t token) {
    range_ = TokenRange{token, token};
    return *this;
}

DiagnosticBuilder &
DiagnosticBuilder::relatedOrigin(camel::source::origin_id_t origin, std::string message) {
    if (origin != camel::source::kInvalidOriginId) {
        related_.push_back(DiagLabel{origin, std::move(message), ""});
    }
    return *this;
}

DiagnosticBuilder &
DiagnosticBuilder::relatedSpan(camel::source::span_id_t span, std::string message) {
    if (span != camel::source::kInvalidSpanId) {
        related_.push_back(DiagLabel{span, std::move(message), ""});
    }
    return *this;
}

DiagnosticBuilder &
DiagnosticBuilder::related(const CharRange &range, std::string message, std::string path) {
    related_.push_back(DiagLabel{range, std::move(message), std::move(path)});
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::related(const TokenRange &range, std::string message) {
    related_.push_back(DiagLabel{range, std::move(message), ""});
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::note(std::string text) {
    if (!text.empty()) {
        notes_.push_back(std::move(text));
    }
    return *this;
}

} // namespace camel::core::error
