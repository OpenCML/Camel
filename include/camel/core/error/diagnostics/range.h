// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/source/ids.h"

#include <string>
#include <variant>
#include <vector>

namespace camel::core::error {

// ---- Basic structures ----
struct CharPos {
    size_t line      = 0;
    size_t character = 0;
};

struct CharRange {
    CharPos start;
    CharPos end;

};

struct TokenRange {
    size_t start = 0;
    size_t end   = 0;
};

using SourceRange = std::variant<
    std::monostate, CharRange, TokenRange, camel::source::span_id_t, camel::source::origin_id_t>;

/// A secondary location of a diagnostic: a place that explains the primary one ("the parameter
/// is declared here", "instantiated by this call"). `message` reads as a sentence fragment that
/// the location completes; the one-line text format prints it as " [message at line:col]".
struct DiagLabel {
    SourceRange range{};
    std::string message;
    /// File of `range` when it is a CharRange in another file than the diagnostic's; a span or
    /// an origin names its own file.
    std::string path;
};

/// One text edit of a fix: replace the text of `range` with `replacement`. The empty range
/// (start == end) inserts, and the empty `replacement` deletes. The ranges of one fix are ranges
/// of the file the diagnostic is located in, and they do not overlap.
struct DiagEdit {
    SourceRange range{};
    std::string replacement;
};

/// A change a report asks for that a tool can apply without asking the reader, in the shape a
/// code action carries: a title that says what the change does, and the edits that carry it out.
/// Every edit is applied to the text as written, never to the text an earlier edit of the same
/// fix produced.
///
/// A fix is offered only where the compiler knows the text to write. `help` says what to do in
/// words and is written for a person; a fix is that same advice where it has exactly one
/// machine-applicable spelling. A report that cannot name the text to write carries none, and
/// says why in `diagnostics/registry.tsv` (see docs/cli.md, "Diagnostics").
struct DiagFix {
    std::string title;
    std::vector<DiagEdit> edits;
};

/// Where the tokens of a file are: what turns a TokenRange into lines and columns. The parser
/// that produced the tokens provides it (camel/parse/parse.h).
class TokenPositions {
  public:
    virtual ~TokenPositions() = default;
    virtual size_t size() const = 0;
    /// From the first character of token `first` to the end of token `last`.
    virtual CharRange charRange(size_t first, size_t last) const = 0;
};

// ---- RangeConverter ----
class RangeConverter {
  public:
    RangeConverter(const TokenPositions &tokens) : tokens_(tokens) {}
    CharRange conv(const TokenRange &tr) const;

  private:
    const TokenPositions &tokens_;
};

} // namespace camel::core::error
