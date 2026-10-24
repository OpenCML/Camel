// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "base.h"
#include "range.h"

#include <string>
#include <vector>

namespace camel::core::error {

struct Diagnostic;
class Diagnostics;

// ---- DiagnosticBuilder ----
class DiagnosticBuilder {
  public:
    DiagnosticBuilder() = default;
    DiagnosticBuilder(Diagnostics *diag) : diagnostics_(diag) {}

    template <typename DiagEnum>
    static DiagnosticBuilder of(DiagEnum err, Diagnostics *diag = nullptr);

    // Position setting methods
    DiagnosticBuilder &at(const CharRange &range);
    DiagnosticBuilder &at(const TokenRange &range);
    DiagnosticBuilder &atSpan(camel::source::span_id_t span);
    DiagnosticBuilder &atOrigin(camel::source::origin_id_t origin);
    DiagnosticBuilder &at(size_t token);

    // Secondary locations and remarks (see DiagLabel). They are taken verbatim: only the
    // message and the suggestion of the catalog entry are format templates.
    DiagnosticBuilder &relatedOrigin(camel::source::origin_id_t origin, std::string message);
    DiagnosticBuilder &relatedSpan(camel::source::span_id_t span, std::string message);
    DiagnosticBuilder &related(const CharRange &range, std::string message, std::string path = "");
    DiagnosticBuilder &related(const TokenRange &range, std::string message);
    DiagnosticBuilder &note(std::string text);

    // The change the report asks for, where the compiler knows the text to write (see DiagFix).
    // `title` says what the change does ("insert ','"); the edit replaces `range` with
    // `replacement`, the empty range inserting and the empty replacement deleting.
    DiagnosticBuilder &fix(std::string title, const CharRange &range, std::string replacement);
    DiagnosticBuilder &fix(std::string title, const TokenRange &range, std::string replacement);
    /// Adds one more edit to the fix added last: a change that has to rewrite two places at once
    /// (a clause that moves) is one fix with several edits.
    DiagnosticBuilder &alsoFix(const CharRange &range, std::string replacement);
    DiagnosticBuilder &alsoFix(const TokenRange &range, std::string replacement);

    // Message substitution + commit
    template <typename... Args> Diagnostic commit(Args &&...args);

  private:
    friend class Diagnostics; // Allow Diagnostics to access private members.

    Diagnostics *diagnostics_;
    SourceRange range_{};
    Severity severity_ = Severity::Error;

    DiagType type_{};
    uint32_t specific_{};
    std::string name_;
    std::string rawMessage_;
    std::string rawSuggestion_;
    std::string message_;
    std::string suggestion_;
    std::string moduleName_;
    std::string modulePath_;
    std::vector<DiagLabel> related_;
    std::vector<std::string> notes_;
    std::vector<DiagFix> fixes_;
};

} // namespace camel::core::error
