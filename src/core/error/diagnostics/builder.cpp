// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
