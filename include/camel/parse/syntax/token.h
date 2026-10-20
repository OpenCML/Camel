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
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Tokens of the hand-written front end.
 *
 * A token is twelve bytes: a byte range of the source text, its kind and two flags that say
 * what stood between it and the token before it. The flags are what the two layout rules of
 * the grammar are decided on (docs/technical/34_handwritten_parser.md):
 *
 *   - SpaceBefore: white space or a comment separates the token from the previous one. A call,
 *     an index, an angle-bracket argument list and the postfix `!` require its absence.
 *   - NewlineBefore: a line break lies between the previous token and this one. It ends a
 *     statement unless the line is visibly unfinished.
 *
 * Comments are tokens too (kinds Command, LineComment, DocComment, BlockComment), kept in the same buffer
 * and skipped by the parser; white space is the text between two tokens. Buffer plus source
 * text is therefore lossless: every byte belongs to a token or lies between two of them.
 */

#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace camel::parse::syntax {

enum class TokenKind : uint8_t {
#define CAMEL_TOKEN(name, display) name,
#define CAMEL_KEYWORD(name, text, group) name,
#define CAMEL_PUNCT(name, text) name,
#include "token_kinds.def"
#undef CAMEL_TOKEN
#undef CAMEL_KEYWORD
#undef CAMEL_PUNCT
    Count_,
};

inline constexpr size_t kTokenKindCount = static_cast<size_t>(TokenKind::Count_);

/// The spelling of a keyword or an operator; a description ("a name") for the other kinds.
std::string_view tokenKindText(TokenKind kind);
/// The enumerator's name ("KwIf", "LParen"): what dumps print.
std::string_view tokenKindName(TokenKind kind);

constexpr bool isKeyword(TokenKind kind) {
    return kind >= TokenKind::KwAs && kind <= TokenKind::KwFalse;
}
constexpr bool isPunctuation(TokenKind kind) {
    return kind >= TokenKind::Semi && kind < TokenKind::Count_;
}
/// Tokens the parser does not see: comments and the text of a lexical error.
constexpr bool isTrivia(TokenKind kind) {
    return kind >= TokenKind::Error && kind <= TokenKind::BlockComment;
}

/// The keyword table as data, for tools that list or check it.
struct KeywordInfo {
    std::string_view text;
    TokenKind kind;
    std::string_view group; // Declaration, Control, Type, Literal, Reserved
};
const std::vector<KeywordInfo> &keywordTable();

struct Token {
    enum Flag : uint8_t {
        SpaceBefore   = 1 << 0,
        NewlineBefore = 1 << 1,
    };

    uint32_t begin = 0; // byte offset of the first byte
    uint32_t end   = 0; // byte offset past the last byte
    TokenKind kind = TokenKind::Eof;
    uint8_t flags  = 0;

    bool spaceBefore() const { return (flags & SpaceBefore) != 0; }
    bool newlineBefore() const { return (flags & NewlineBefore) != 0; }
    uint32_t length() const { return end - begin; }
};

enum class LexErrorKind : uint8_t {
    IllegalCharacter,    // a run of characters that start no token
    UnclosedString,      // a quote without its closing quote
    UnterminatedComment, // `/*` without `*/`
};

struct LexError {
    LexErrorKind kind;
    uint32_t begin = 0;
    uint32_t end   = 0;
};

/// One `${...}` of a formatted string (`FString` token), as the lexer read it. The lexer finds
/// the matching `}` by scanning Camel source (strings, comments and nested braces included) and
/// then lexes the text to split the expression from the optional format specification at the
/// first `:` outside every bracket.
struct FStringInterpolation {
    uint32_t token = 0;     // index of the FString token in TokenBuffer::tokens()
    uint32_t open  = 0;     // byte offset of the '$' of `${`
    uint32_t exprBegin = 0; // the expression: just after `${`
    uint32_t exprEnd   = 0; // at the ':' of the specification, or at the closing '}'
    uint32_t specBegin = 0; // just after ':' (empty when there is no specification)
    uint32_t specEnd   = 0; // at the closing '}'
    uint32_t close     = 0; // the '}' that ends the interpolation
    bool hasSpec       = false;
};

/// The tokens of one source text, with the text and its line table.
class TokenBuffer {
  public:
    TokenBuffer() = default;
    explicit TokenBuffer(std::string source) : source_(std::move(source)) {}

    const std::string &source() const { return source_; }
    const std::vector<Token> &tokens() const { return tokens_; }
    const std::vector<LexError> &errors() const { return errors_; }
    /// Every `${...}` of every formatted string of this buffer, in source order.
    const std::vector<FStringInterpolation> &interpolations() const { return interpolations_; }
    /// The interpolations of the FString token `token`.
    std::vector<FStringInterpolation> interpolationsOf(uint32_t token) const;
    size_t size() const { return tokens_.size(); }
    const Token &operator[](size_t index) const { return tokens_[index]; }

    std::string_view text(const Token &token) const {
        return std::string_view(source_).substr(token.begin, token.end - token.begin);
    }
    std::string_view text(size_t index) const { return text(tokens_[index]); }

    /// Zero-based line of a byte offset.
    size_t lineOf(uint32_t offset) const;
    /// Zero-based column of a byte offset, counted in code points (as diagnostics count).
    size_t columnOf(uint32_t offset) const;
    size_t lineCount() const { return lineStarts_.size(); }
    uint32_t lineStart(size_t line) const { return lineStarts_[line]; }

    /// Index of the next / previous token that is not trivia; size() / SIZE_MAX if none.
    size_t nextSignificant(size_t index) const;
    /// The documentation of the declaration whose first token is `index`: the `///` lines
    /// right above it, one per line without a gap, without the marker and one space after
    /// it. Empty if there is none.
    std::string docAbove(size_t index) const;
    size_t previousSignificant(size_t index) const;

  private:
    friend class Lexer;
    std::string source_;
    std::vector<Token> tokens_;
    std::vector<LexError> errors_;
    std::vector<FStringInterpolation> interpolations_;
    std::vector<uint32_t> lineStarts_{0};
};

} // namespace camel::parse::syntax
