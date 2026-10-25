// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

std::vector<FStringInterpolation> TokenBuffer::interpolationsOf(uint32_t token) const {
    std::vector<FStringInterpolation> out;
    for (const FStringInterpolation &part : interpolations_) {
        if (part.token == token) {
            out.push_back(part);
        }
    }
    return out;
}

namespace {

/// The end of the string that starts at `i` (s[i] is `'`, `"` or a backtick), just past its
/// closing quote; npos when it does not close. It is what the lexer does, without the fallback
/// of an unclosed string: used to skip the strings inside a `${...}`.
size_t skipQuoted(const char *s, size_t n, size_t i);

/// Finds the `}` that ends the `${` at `open`; `close` is set to it. Strings, comments and
/// nested braces are skipped, so `${ { a: 1 } }` and `${ '}' }` end where they should.
bool scanInterpolation(const char *s, size_t n, size_t open, size_t &close);

/// The end of the formatted string that starts at `i` (s[i] is a backtick), nested `${...}`
/// included; npos when it does not close.
size_t skipFString(const char *s, size_t n, size_t i) {
    for (size_t j = i + 1; j < n;) {
        const char c = s[j];
        if (c == '`') {
            return j + 1;
        }
        if (c == '\\' && j + 1 < n) {
            if (j + 2 < n && s[j + 1] == '$' && s[j + 2] == '{') {
                j += 3; // `\${` is a literal `${`
                continue;
            }
            if (s[j + 1] == '`' || s[j + 1] == '\\') {
                j += 2;
                continue;
            }
            ++j;
            continue;
        }
        if (c == '$' && j + 1 < n && s[j + 1] == '{') {
            size_t close = 0;
            if (!scanInterpolation(s, n, j, close)) {
                return std::string_view::npos;
            }
            j = close + 1;
            continue;
        }
        ++j;
    }
    return std::string_view::npos;
}

size_t skipQuoted(const char *s, size_t n, size_t i) {
    const char c = s[i];
    if (c == '`') {
        return skipFString(s, n, i);
    }
    if (i + 2 < n && s[i + 1] == c && s[i + 2] == c) {
        for (size_t j = i + 3; j + 2 < n; ++j) {
            if (s[j] == c && s[j + 1] == c && s[j + 2] == c) {
                return j + 3;
            }
        }
        return i + 2; // no closing triple: the first two quotes are an empty string
    }
    for (size_t j = i + 1; j < n; ++j) {
        if (s[j] == c) {
            return j + 1;
        }
        if (s[j] == '\\' && j + 1 < n && (s[j + 1] == c || s[j + 1] == '\\')) {
            ++j;
        }
    }
    return std::string_view::npos;
}

/// Finds the `}` that ends the `${` at `open`; `close` is set to it. Strings, comments and
/// nested braces are skipped, so `${ { a: 1 } }` and `${ '}' }` end where they should.
bool scanInterpolation(const char *s, size_t n, size_t open, size_t &close) {
    size_t j  = open + 2;
    int depth = 0;
    while (j < n) {
        const char c = s[j];
        if (c == '\'' || c == '"' || c == '`') {
            const size_t e = skipQuoted(s, n, j);
            if (e == std::string_view::npos) {
                return false;
            }
            j = e;
            continue;
        }
        if (c == '/' && j + 1 < n) {
            if (s[j + 1] == '/') {
                while (j < n && s[j] != '\n' && s[j] != '\r') {
                    ++j;
                }
                continue;
            }
            if (s[j + 1] == '*') {
                const std::string_view rest(s + j + 2, n - j - 2);
                const size_t end = rest.find("*/");
                if (end == std::string_view::npos) {
                    return false;
                }
                j += 2 + end + 2;
                continue;
            }
        }
        if (c == '{') {
            ++depth;
        } else if (c == '}') {
            if (depth == 0) {
                close = j;
                return true;
            }
            --depth;
        }
        ++j;
    }
    return false;
}

/// Splits `${EXPR[:SPEC]}` at the first `:` that is outside every bracket, as the token kinds
/// of the content say (so a `:` of a key/value pair inside `{}` or of a lambda inside `()`
/// does not split). `LAngle`/`Gt` are the lexer's own angle-bracket argument lists.
void splitInterpolation(const std::string &source, FStringInterpolation &part) {
    const TokenBuffer sub = Lexer::tokenizeRange(source, part.exprBegin, part.close);
    std::vector<TokenKind> open;
    for (const Token &token : sub.tokens()) {
        if (token.kind == TokenKind::Eof) {
            break;
        }
        switch (token.kind) {
        case TokenKind::LParen:
        case TokenKind::LBracket:
        case TokenKind::LBrace:
        case TokenKind::LAngle:
            open.push_back(token.kind);
            break;
        case TokenKind::RParen:
        case TokenKind::RBracket:
        case TokenKind::RBrace:
            if (!open.empty()) {
                open.pop_back();
            }
            break;
        case TokenKind::Gt:
            if (!open.empty() && open.back() == TokenKind::LAngle) {
                open.pop_back();
            }
            break;
        case TokenKind::Colon:
            if (open.empty()) {
                part.hasSpec   = true;
                part.exprEnd   = token.begin;
                part.specBegin = token.end;
                part.specEnd   = part.close;
                return;
            }
            break;
        default:
            break;
        }
    }
    part.exprEnd   = part.close;
    part.specBegin = part.close;
    part.specEnd   = part.close;
}

} // namespace

/// Tokenizes `source`; with `from`/`to`, only that byte range (the whole text is still the
/// buffer's source, so positions are those of the file).
TokenBuffer Lexer::tokenizeImpl(std::string source, size_t from, size_t to) {
    TokenBuffer buffer(std::move(source));
    const char *const s = buffer.source_.data();
    const size_t full   = buffer.source_.size();
    const size_t n      = to;
    auto &tokens = buffer.tokens_;
    auto &lines  = buffer.lineStarts_;
    if (from > 0) {
        for (size_t k = 0; k < from; ++k) {
            if (s[k] == '\n') {
                lines.push_back(static_cast<uint32_t>(k + 1));
            }
        }
    }
    tokens.reserve(n / 4 + 16);
    lines.reserve(n / 24 + 16);

    uint8_t pending    = 0;              // what separates the next token from the last one
    TokenKind previous = TokenKind::Eof; // kind of the last token that is not trivia
    size_t i           = from;

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
                if (at(k) == 'i' && !isIdentPart(at(k + 1))) {
                    // `4.5i`: an imaginary literal. The suffix belongs to the number only when
                    // no identifier character follows, so `4if` stays `4` and `if`.
                    emit(TokenKind::Imaginary, i, k + 1);
                    i = k + 1;
                    continue;
                }
                emit(TokenKind::Real, i, k);
                i = k;
                continue;
            }
            // INTEGER: decimal (`0`, `42`), `0x` and hexadecimal digits, `0b` and binary
            // digits, `0o` and octal digits; then an exponent on a decimal number. A decimal
            // digit run that starts with `0` and has more than that one digit (`09`, `077`) is
            // not a number token: it is refused here as a whole, with a located error, instead
            // of being cut into `0` and `9`. Octal is written `0o77`. A prefixed form
            // with no digit of its base is refused the same way instead of being cut.
            size_t end   = j;
            bool invalid = false;
            if (c == '0') {
                if (at(i + 1) == 'x') {
                    end = i + 2;
                    while (isHexDigit(at(end))) {
                        ++end;
                    }
                    if (end == i + 2) {
                        invalid = true;
                    }
                    // `0xFFe-3`: the longest match gives the final `e` to a signed exponent
                    // (the literal is rejected later: an exponent is decimal only).
                    if (!invalid && (at(end - 1) == 'e' || at(end - 1) == 'E') && end - 1 > i + 2 &&
                        (at(end) == '+' || at(end) == '-') && isDigit(at(end + 1))) {
                        --end;
                    }
                } else if (at(i + 1) == 'b') {
                    end = i + 2;
                    while (at(end) == '0' || at(end) == '1') {
                        ++end;
                    }
                    if (end == i + 2 || isDigit(at(end))) {
                        invalid = true;
                        while (isDigit(at(end))) {
                            ++end;
                        }
                    }
                } else if (at(i + 1) == 'o') {
                    end = i + 2;
                    while (at(end) >= '0' && at(end) <= '7') {
                        ++end;
                    }
                    if (end == i + 2 || at(end) == '8' || at(end) == '9') {
                        invalid = true;
                        while (isDigit(at(end))) {
                            ++end;
                        }
                    }
                } else if (isDigit(at(i + 1))) {
                    // A leading-zero decimal run: `09`, `077`, `077e2`, `09i`. It is refused as
                    // one token, so a source program cannot quietly read `0` and then a stray
                    // statement; the entry reader and a conversion text read the same digits as
                    // decimal data text (reference 2.5.2).
                    invalid = true;
                    end     = exponentEnd(j);
                    if (at(end) == 'i' && !isIdentPart(at(end + 1))) {
                        ++end;
                    }
                } else {
                    end = i + 1; // `0` itself
                }
            }
            if (invalid) {
                // A prefixed form with no digits after it consumes an identifier-like tail so
                // that the diagnostic covers the whole written run (`0x`, `0xG`).
                if (end == i + 2) {
                    while (isIdentPart(at(end))) {
                        ++end;
                    }
                }
                buffer.errors_.push_back(
                    LexError{
                        LexErrorKind::InvalidNumber,
                        static_cast<uint32_t>(i),
                        static_cast<uint32_t>(end)});
                emitTrivia(TokenKind::Error, i, end, false);
                i = end;
                continue;
            }
            // A decimal number with an exponent is a floating-point literal (`1e5` is
            // 100000.0). On a hexadecimal, binary or octal number an exponent is an error
            // the AST builder reports; the token stays an integer for it.
            const size_t digitsEnd = end;
            end                    = exponentEnd(end);
            const bool decimal     = c != '0' || digitsEnd == i + 1;
            if (decimal && at(end) == 'i' && !isIdentPart(at(end + 1))) {
                // `4i`: an imaginary literal (a decimal number only; `0xFFi` is `0xFF` and the
                // name `i`).
                emit(TokenKind::Imaginary, i, end + 1);
                i = end + 1;
                continue;
            }
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
            if (c == '`') {
                // FSTRING: `${EXPR[:SPEC]}` starts an interpolation, whose content is ordinary
                // Camel source (strings and braces nest); `\${` is a literal `${`; other
                // braces are literal; it may span lines. The interpolation entries remember
                // where each expression is, for the AST and the language server.
                size_t j           = i + 1;
                size_t fallbackEnd = 0;
                bool closed        = false;
                std::vector<FStringInterpolation> parts;
                while (j < n) {
                    const char d = s[j];
                    if (d == '`') {
                        closed = true;
                        ++j;
                        break;
                    }
                    if (d == '\\' && j + 1 < n) {
                        if (j + 2 < n && s[j + 1] == '$' && s[j + 2] == '{') {
                            j += 3;
                            continue;
                        }
                        if (s[j + 1] == '`') {
                            fallbackEnd = j + 2;
                            j += 2;
                            continue;
                        }
                        if (s[j + 1] == '\\') {
                            j += 2;
                            continue;
                        }
                        ++j;
                        continue;
                    }
                    if (d == '$' && j + 1 < n && s[j + 1] == '{') {
                        size_t close = 0;
                        if (!scanInterpolation(s, n, j, close)) {
                            break; // no matching `}`: the whole token is unclosed
                        }
                        FStringInterpolation part;
                        part.open      = static_cast<uint32_t>(j);
                        part.exprBegin = static_cast<uint32_t>(j + 2);
                        part.close     = static_cast<uint32_t>(close);
                        parts.push_back(part);
                        j = close + 1;
                        continue;
                    }
                    ++j;
                }
                if (!closed && fallbackEnd != 0) {
                    j      = fallbackEnd;
                    closed = true;
                }
                if (closed) {
                    noteLines(i, j);
                    const uint32_t tokenIndex = static_cast<uint32_t>(tokens.size());
                    for (FStringInterpolation &part : parts) {
                        part.token = tokenIndex;
                        splitInterpolation(buffer.source_, part);
                        buffer.interpolations_.push_back(part);
                    }
                    emit(TokenKind::FString, i, j);
                    i = j;
                    continue;
                }
                buffer.errors_.push_back(
                    {LexErrorKind::UnclosedString,
                     static_cast<uint32_t>(i),
                     static_cast<uint32_t>(n)});
                emitTrivia(TokenKind::Error, i, n, noteLines(i, n));
                i = n;
                continue;
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
            // a tag (design principles 4.1), so it is an ordinary token.
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
                if (c1 == '=' && c2 == '>') {
                    three(TokenKind::Equiv);
                } else if (c1 == '=') {
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
    // A partial lex keeps the line table of the whole text, so a position inside the range is
    // the position in the file.
    for (size_t k = n; k < full; ++k) {
        if (s[k] == '\n') {
            lines.push_back(static_cast<uint32_t>(k + 1));
        }
    }
    return buffer;
}

TokenBuffer Lexer::tokenize(std::string source) {
    const size_t size = source.size();
    return tokenizeImpl(std::move(source), 0, size);
}

TokenBuffer Lexer::tokenizeRange(std::string source, size_t begin, size_t end) {
    const size_t size = source.size();
    return tokenizeImpl(std::move(source), std::min(begin, size), std::min(end, size));
}

} // namespace camel::parse::syntax
