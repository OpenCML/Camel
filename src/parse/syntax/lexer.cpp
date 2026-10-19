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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/parse/syntax/lexer.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace camel::parse::syntax {

namespace {

struct KindText {
    std::string_view name;
    std::string_view text;
};

constexpr std::array<KindText, kTokenKindCount> kKindText = {{
#define CAMEL_TOKEN(name, display) {#name, display},
#define CAMEL_KEYWORD(name, text, group) {#name, text},
#define CAMEL_PUNCT(name, text) {#name, text},
#include "camel/parse/syntax/token_kinds.def"
#undef CAMEL_TOKEN
#undef CAMEL_KEYWORD
#undef CAMEL_PUNCT
}};

// ---- Character classes ----

enum CharClass : uint8_t {
    kIllegal = 0,
    kBlank,
    kNewline,
    kIdentStart,
    kDigit,
    kQuote,
    kPunct,
    kHash,
};

constexpr std::array<uint8_t, 256> makeCharClasses() {
    std::array<uint8_t, 256> table{};
    for (int c = 0; c < 256; ++c) {
        table[c] = kIllegal;
    }
    table[' '] = table['\t'] = table['\r'] = table['\f'] = kBlank;
    table['\n']                                          = kNewline;
    for (int c = 'a'; c <= 'z'; ++c) {
        table[c] = kIdentStart;
    }
    for (int c = 'A'; c <= 'Z'; ++c) {
        table[c] = kIdentStart;
    }
    table['_'] = kIdentStart;
    for (int c = '0'; c <= '9'; ++c) {
        table[c] = kDigit;
    }
    table['\''] = table['"'] = table['`'] = kQuote;
    for (char c : std::string_view(";,:.()[]{}<>=+-*/%^@&|!~?\\")) {
        table[static_cast<unsigned char>(c)] = kPunct;
    }
    table['#'] = kHash;
    return table;
}

constexpr std::array<uint8_t, 256> kCharClass = makeCharClasses();

constexpr bool isIdentPart(unsigned char c) {
    return kCharClass[c] == kIdentStart || kCharClass[c] == kDigit;
}
constexpr bool isDigit(unsigned char c) { return c >= '0' && c <= '9'; }
constexpr bool isHexDigit(unsigned char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

// ---- Keywords ----

// Open addressing over a table several times the number of keywords; the hash reads the
// length and three characters, so a lookup is a few instructions and one comparison.
constexpr size_t kKeywordSlots = 256;

constexpr uint32_t keywordHash(const char *text, size_t length) {
    uint32_t h = static_cast<uint32_t>(length) * 131u;
    h += static_cast<unsigned char>(text[0]) * 31u;
    h += static_cast<unsigned char>(text[length - 1]) * 7u;
    h += static_cast<unsigned char>(text[length / 2]) * 3u;
    return h & (kKeywordSlots - 1);
}

struct KeywordSlot {
    const char *text = nullptr;
    uint8_t length   = 0;
    TokenKind kind   = TokenKind::Identifier;
};

const std::array<KeywordSlot, kKeywordSlots> &keywordSlots() {
    static const std::array<KeywordSlot, kKeywordSlots> slots = [] {
        std::array<KeywordSlot, kKeywordSlots> table{};
        for (const KeywordInfo &info : keywordTable()) {
            uint32_t h = keywordHash(info.text.data(), info.text.size());
            while (table[h].text != nullptr) {
                h = (h + 1) & (kKeywordSlots - 1);
            }
            table[h] = {info.text.data(), static_cast<uint8_t>(info.text.size()), info.kind};
        }
        return table;
    }();
    return slots;
}

inline TokenKind classifyWord(const char *text, size_t length) {
    // The shortest keywords have two letters, the longest is `namespace`.
    if (length < 2 || length > 9) {
        return (length == 1 && text[0] == '_') ? TokenKind::Underscore : TokenKind::Identifier;
    }
    static const auto &slots = keywordSlots();
    for (uint32_t h = keywordHash(text, length);; h = (h + 1) & (kKeywordSlots - 1)) {
        const KeywordSlot &slot = slots[h];
        if (slot.text == nullptr) {
            return TokenKind::Identifier;
        }
        if (slot.length == length && std::memcmp(slot.text, text, length) == 0) {
            return slot.kind;
        }
    }
}

/// Tokens after which a `<` that touches them opens an angle-bracket argument list: the tokens
/// an operand can end with (rule 1).
constexpr bool endsAngleHost(TokenKind kind) {
    switch (kind) {
    case TokenKind::Identifier:
    case TokenKind::Integer:
    case TokenKind::Real:
    case TokenKind::String:
    case TokenKind::MultiStr:
    case TokenKind::FString:
    case TokenKind::KwTrue:
    case TokenKind::KwFalse:
    case TokenKind::KwNull:
    case TokenKind::RParen:
    case TokenKind::RBracket:
    case TokenKind::RBrace:
    case TokenKind::Gt:
    case TokenKind::Bang:
        return true;
    default:
        return false;
    }
}

} // namespace

std::string_view tokenKindText(TokenKind kind) { return kKindText[static_cast<size_t>(kind)].text; }
std::string_view tokenKindName(TokenKind kind) { return kKindText[static_cast<size_t>(kind)].name; }

const std::vector<KeywordInfo> &keywordTable() {
    static const std::vector<KeywordInfo> table = {
#define CAMEL_TOKEN(name, display)
#define CAMEL_KEYWORD(name, text, group) {text, TokenKind::name, #group},
#define CAMEL_PUNCT(name, text)
#include "camel/parse/syntax/token_kinds.def"
#undef CAMEL_TOKEN
#undef CAMEL_KEYWORD
#undef CAMEL_PUNCT
    };
    return table;
}

size_t TokenBuffer::lineOf(uint32_t offset) const {
    auto it = std::upper_bound(lineStarts_.begin(), lineStarts_.end(), offset);
    return static_cast<size_t>(it - lineStarts_.begin()) - 1;
}

size_t TokenBuffer::columnOf(uint32_t offset) const {
    size_t column = 0;
    for (uint32_t i = lineStarts_[lineOf(offset)]; i < offset && i < source_.size(); ++i) {
        if ((static_cast<unsigned char>(source_[i]) & 0xC0) != 0x80) {
            ++column;
        }
    }
    return column;
}

size_t TokenBuffer::nextSignificant(size_t index) const {
    while (index < tokens_.size() && isTrivia(tokens_[index].kind)) {
        ++index;
    }
    return index;
}

std::string TokenBuffer::docAbove(size_t index) const {
    if (index >= tokens_.size()) {
        return {};
    }
    std::vector<std::string_view> lines;
    size_t expectLine = lineOf(tokens_[index].begin);
    for (size_t i = index; i-- > 0;) {
        const Token &token = tokens_[i];
        const size_t line  = lineOf(token.begin);
        if (token.kind != TokenKind::DocComment || line + 1 != expectLine) {
            break;
        }
        std::string_view body = text(token).substr(3);
        if (!body.empty() && body.front() == ' ') {
            body.remove_prefix(1);
        }
        while (!body.empty() && (body.back() == '\r' || body.back() == ' ')) {
            body.remove_suffix(1);
        }
        lines.push_back(body);
        expectLine = line;
    }
    std::string doc;
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
        if (!doc.empty()) {
            doc += '\n';
        }
        doc.append(*it);
    }
    return doc;
}

size_t TokenBuffer::previousSignificant(size_t index) const {
    while (index != SIZE_MAX && index < tokens_.size() && isTrivia(tokens_[index].kind)) {
        index = index == 0 ? SIZE_MAX : index - 1;
    }
    return index;
}

TokenBuffer Lexer::tokenize(std::string source) {
    TokenBuffer buffer(std::move(source));
    const char *const s = buffer.source_.data();
    const size_t n      = buffer.source_.size();
    auto &tokens        = buffer.tokens_;
    auto &lines         = buffer.lineStarts_;
    tokens.reserve(n / 4 + 16);
    lines.reserve(n / 24 + 16);

    uint8_t pending    = 0;              // what separates the next token from the last one
    TokenKind previous = TokenKind::Eof; // kind of the last token that is not trivia
    size_t i           = 0;

    auto at = [&](size_t index) -> unsigned char {
        return index < n ? static_cast<unsigned char>(s[index]) : 0;
    };
    auto emit = [&](TokenKind kind, size_t begin, size_t end) {
        tokens.push_back(
            Token{static_cast<uint32_t>(begin), static_cast<uint32_t>(end), kind, pending});
        pending  = 0;
        previous = kind;
    };
    // A comment or the text of an error: it does not reset `pending`, it adds to it.
    auto emitTrivia = [&](TokenKind kind, size_t begin, size_t end, bool spansLines) {
        tokens.push_back(
            Token{static_cast<uint32_t>(begin), static_cast<uint32_t>(end), kind, pending});
        pending |= Token::SpaceBefore;
        if (spansLines) {
            pending |= Token::NewlineBefore;
        }
    };
    auto noteLines = [&](size_t begin, size_t end) {
        bool any = false;
        for (size_t k = begin; k < end; ++k) {
            const void *found = std::memchr(s + k, '\n', end - k);
            if (found == nullptr) {
                break;
            }
            k = static_cast<size_t>(static_cast<const char *>(found) - s);
            lines.push_back(static_cast<uint32_t>(k + 1));
            any = true;
        }
        return any;
    };
    // [eE][+-]?[0-9]+ at `j`, or nothing.
    auto exponentEnd = [&](size_t j) -> size_t {
        if (at(j) != 'e' && at(j) != 'E') {
            return j;
        }
        size_t k = j + 1;
        if (at(k) == '+' || at(k) == '-') {
            ++k;
        }
        if (!isDigit(at(k))) {
            return j;
        }
        while (isDigit(at(k))) {
            ++k;
        }
        return k;
    };

    while (i < n) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        switch (kCharClass[c]) {
        case kBlank:
            pending |= Token::SpaceBefore;
            ++i;
            continue;
        case kNewline:
            pending |= Token::SpaceBefore | Token::NewlineBefore;
            lines.push_back(static_cast<uint32_t>(++i));
            continue;
        case kIdentStart: {
            size_t j = i + 1;
            while (j < n && isIdentPart(static_cast<unsigned char>(s[j]))) {
                ++j;
            }
            emit(classifyWord(s + i, j - i), i, j);
            i = j;
            continue;
        }
        case kDigit: {
            // REAL: digits '.' digits, with an exponent; it is the longest match whenever it
            // applies (`007.5` is a real). `1.` and `1..2` leave the dot to the next token.
            size_t j = i + 1;
            while (isDigit(at(j))) {
                ++j;
            }
            if (at(j) == '.' && isDigit(at(j + 1))) {
                size_t k = j + 2;
                while (isDigit(at(k))) {
                    ++k;
                }
                k = exponentEnd(k);
                emit(TokenKind::Real, i, k);
                i = k;
                continue;
            }
            // INTEGER: decimal without a leading zero, `0`, `0` and octal digits, `0x` and
            // hexadecimal digits, `0b` and binary digits; then an exponent. A digit run that
            // is none of them is cut where the longest of them ends (`089` is `0`, `89`).
            size_t end = j;
            if (c == '0') {
                if (at(i + 1) == 'x' && isHexDigit(at(i + 2))) {
                    end = i + 3;
                    while (isHexDigit(at(end))) {
                        ++end;
                    }
                    // `0xFFe-3`: the longest match gives the final `e` to a signed exponent
                    // (the literal is rejected later: an exponent is decimal only).
                    if ((at(end - 1) == 'e' || at(end - 1) == 'E') && end - 1 > i + 2 &&
                        (at(end) == '+' || at(end) == '-') && isDigit(at(end + 1))) {
                        --end;
                    }
                } else if (at(i + 1) == 'b' && (at(i + 2) == '0' || at(i + 2) == '1')) {
                    end = i + 3;
                    while (at(end) == '0' || at(end) == '1') {
                        ++end;
                    }
                } else {
                    end = i + 1;
                    while (at(end) >= '0' && at(end) <= '7') {
                        ++end;
                    }
                }
            }
            // A decimal number with an exponent is a floating-point literal (`1e5` is
            // 100000.0). On a hexadecimal, binary or octal number an exponent is an error
            // the AST builder reports; the token stays an integer for it.
            const size_t digitsEnd = end;
            end                    = exponentEnd(end);
            const bool decimal     = c != '0' || digitsEnd == i + 1;
            emit(end > digitsEnd && decimal ? TokenKind::Real : TokenKind::Integer, i, end);
            i = end;
            continue;
        }
        case kQuote: {
            // '''...''' and """...""": to the first closing triple, no escapes.
            if (c != '`' && at(i + 1) == c && at(i + 2) == c) {
                const char triple[3] = {
                    static_cast<char>(c),
                    static_cast<char>(c),
                    static_cast<char>(c)};
                const std::string_view rest(s + i + 3, n - i - 3);
                const size_t close = rest.find(std::string_view(triple, 3));
                if (close != std::string_view::npos) {
                    const size_t end = i + 3 + close + 3;
                    noteLines(i, end);
                    emit(TokenKind::MultiStr, i, end);
                    i = end;
                    continue;
                }
                // No closing triple: the first two quotes are an empty string.
            }
            // '...', "...", `...`: to the closing quote; `\\` and an escaped quote do not
            // close. A string may span lines. When no quote closes it, the string ends at its
            // last escaped quote if it has one ('C:\dir\' is a string), as it always did.
            size_t j               = i + 1;
            size_t fallbackEnd     = 0;
            size_t fallbackLines   = 0;
            const size_t lineCount = lines.size();
            bool closed            = false;
            while (j < n) {
                const unsigned char d = static_cast<unsigned char>(s[j]);
                if (d == c) {
                    closed = true;
                    ++j;
                    break;
                }
                if (d == '\\' && j + 1 < n) {
                    const unsigned char e = static_cast<unsigned char>(s[j + 1]);
                    if (e == c) {
                        j += 2;
                        fallbackEnd   = j;
                        fallbackLines = lines.size();
                        continue;
                    }
                    if (e == '\\') {
                        j += 2;
                        continue;
                    }
                }
                if (d == '\n') {
                    lines.push_back(static_cast<uint32_t>(j + 1));
                }
                ++j;
            }
            if (!closed && fallbackEnd != 0) {
                lines.resize(fallbackLines);
                j      = fallbackEnd;
                closed = true;
            }
            if (closed) {
                emit(c == '`' ? TokenKind::FString : TokenKind::String, i, j);
                i = j;
                continue;
            }
            // Unclosed: everything to the end of the file is the error.
            buffer.errors_.push_back(
                {LexErrorKind::UnclosedString, static_cast<uint32_t>(i), static_cast<uint32_t>(n)});
            emitTrivia(TokenKind::Error, i, n, lines.size() > lineCount);
            i = n;
            continue;
        }
        case kHash: {
            // `#!` on the first line is an interpreter line and is ignored; elsewhere `#` opens
            // a tag (design principles 2.20), so it is an ordinary token.
            if (i == 0 && at(1) == '!') {
                size_t j = i + 1;
                while (j < n && s[j] != '\n' && s[j] != '\r' && s[j] != '\f') {
                    ++j;
                }
                emitTrivia(TokenKind::Command, i, j, false);
                i = j;
                continue;
            }
            emit(TokenKind::Hash, i, i + 1);
            ++i;
            continue;
        }
        case kPunct: {
            const unsigned char c1 = at(i + 1);
            const unsigned char c2 = at(i + 2);
            TokenKind kind         = TokenKind::Error;
            size_t length          = 1;
            auto one               = [&](TokenKind k) { kind = k; };
            auto two               = [&](TokenKind k) {
                kind   = k;
                length = 2;
            };
            auto three = [&](TokenKind k) {
                kind   = k;
                length = 3;
            };
            switch (c) {
            case ';':
                one(TokenKind::Semi);
                break;
            case ',':
                one(TokenKind::Comma);
                break;
            case '(':
                one(TokenKind::LParen);
                break;
            case ')':
                one(TokenKind::RParen);
                break;
            case '[':
                one(TokenKind::LBracket);
                break;
            case ']':
                one(TokenKind::RBracket);
                break;
            case '{':
                one(TokenKind::LBrace);
                break;
            case '}':
                one(TokenKind::RBrace);
                break;
            case '~':
                one(TokenKind::Tilde);
                break;
            case '\\':
                one(TokenKind::Backslash);
                break;
            case ':':
                c1 == ':' ? two(TokenKind::ColonColon) : one(TokenKind::Colon);
                break;
            case '.':
                if (c1 == '.' && c2 == '.') {
                    three(TokenKind::Ellipsis);
                } else {
                    c1 == '.' ? two(TokenKind::DotDot) : one(TokenKind::Dot);
                }
                break;
            case '=':
                if (c1 == '=' && c2 == '=') {
                    three(TokenKind::EqEqEq);
                } else if (c1 == '=') {
                    two(TokenKind::EqEq);
                } else {
                    c1 == '>' ? two(TokenKind::FatArrow) : one(TokenKind::Assign);
                }
                break;
            case '<':
                // Rule 1: `<=` is always the comparison; a lone `<` opens an argument list
                // exactly when it touches a token an operand can end with.
                if (c1 == '=') {
                    two(TokenKind::LtEq);
                } else {
                    one((pending == 0 && endsAngleHost(previous)) ? TokenKind::LAngle
                                                                  : TokenKind::Lt);
                }
                break;
            case '>':
                c1 == '=' ? two(TokenKind::GtEq) : one(TokenKind::Gt);
                break;
            case '+':
                c1 == '=' ? two(TokenKind::PlusAssign) : one(TokenKind::Plus);
                break;
            case '-':
                if (c1 == '=') {
                    two(TokenKind::MinusAssign);
                } else {
                    c1 == '>' ? two(TokenKind::Arrow) : one(TokenKind::Minus);
                }
                break;
            case '*':
                c1 == '=' ? two(TokenKind::StarAssign) : one(TokenKind::Star);
                break;
            case '/':
                if (c1 == '/') {
                    size_t j = i + 2;
                    while (j < n && s[j] != '\n' && s[j] != '\r') {
                        ++j;
                    }
                    const bool doc = at(i + 2) == '/' && at(i + 3) != '/';
                    emitTrivia(doc ? TokenKind::DocComment : TokenKind::LineComment, i, j, false);
                    i = j;
                    continue;
                }
                if (c1 == '*') {
                    const std::string_view rest(s + i + 2, n - i - 2);
                    const size_t close = rest.find("*/");
                    if (close == std::string_view::npos) {
                        buffer.errors_.push_back(
                            {LexErrorKind::UnterminatedComment,
                             static_cast<uint32_t>(i),
                             static_cast<uint32_t>(n)});
                        emitTrivia(TokenKind::Error, i, n, noteLines(i, n));
                        i = n;
                        continue;
                    }
                    const size_t end = i + 2 + close + 2;
                    emitTrivia(TokenKind::BlockComment, i, end, noteLines(i, end));
                    i = end;
                    continue;
                }
                c1 == '=' ? two(TokenKind::SlashAssign) : one(TokenKind::Slash);
                break;
            case '%':
                c1 == '=' ? two(TokenKind::PercentAssign) : one(TokenKind::Percent);
                break;
            case '^':
                c1 == '=' ? two(TokenKind::CaretAssign) : one(TokenKind::Caret);
                break;
            case '@':
                c1 == '=' ? two(TokenKind::AtAssign) : one(TokenKind::At);
                break;
            case '&':
                if (c1 == '&') {
                    two(TokenKind::AmpAmp);
                } else {
                    c1 == '=' ? two(TokenKind::AmpAssign) : one(TokenKind::Amp);
                }
                break;
            case '|':
                if (c1 == '|') {
                    two(TokenKind::PipePipe);
                } else {
                    c1 == '=' ? two(TokenKind::PipeAssign) : one(TokenKind::Pipe);
                }
                break;
            case '!':
                if (c1 == '=' && c2 == '=') {
                    three(TokenKind::NotEqEq);
                } else if (c1 == '=') {
                    two(TokenKind::NotEq);
                } else {
                    c1 == '!' ? two(TokenKind::BangBang) : one(TokenKind::Bang);
                }
                break;
            case '?':
                if (c1 == '-' && c2 == '>') {
                    three(TokenKind::QuestionArrow);
                } else if (c1 == '.' && c2 == '.') {
                    three(TokenKind::QuestionDotDot);
                } else {
                    c1 == '?' ? two(TokenKind::QuestionQuestion) : one(TokenKind::Question);
                }
                break;
            default:
                break;
            }
            emit(kind, i, i + length);
            i += length;
            continue;
        }
        case kIllegal:
        default: {
            // A run of characters that start no token is one error (a word in another script).
            size_t j = i;
            while (j < n && kCharClass[static_cast<unsigned char>(s[j])] == kIllegal) {
                ++j;
            }
            buffer.errors_.push_back(
                {LexErrorKind::IllegalCharacter,
                 static_cast<uint32_t>(i),
                 static_cast<uint32_t>(j)});
            emitTrivia(TokenKind::Error, i, j, false);
            i = j;
            continue;
        }
        }
    }
    tokens.push_back(
        Token{static_cast<uint32_t>(n), static_cast<uint32_t>(n), TokenKind::Eof, pending});
    return buffer;
}

} // namespace camel::parse::syntax
