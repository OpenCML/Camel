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
 * `camel-parsediff lexdiff`: the hand-written lexer against the ANTLR lexer, token for token.
 *
 * Both token streams are reduced to (kind, first byte, last byte) and compared. The kind of a
 * keyword or an operator is its text; `LAngle` and `Lt` are both `<`, since the distinction is
 * the one thing the new lexer adds (rule 1). The text of a lexical error is in neither stream:
 * ANTLR drops it and the new lexer keeps it as an Error token, skipped here. A file with an
 * unterminated block comment is the one intended difference: ANTLR reads its `/*` as the
 * operators `/` and `*`, the new lexer reports the comment.
 */

#include "modes.h"

#include "antlr4-runtime/antlr4-runtime.h"
#include "camel/parse/antlr/OpenCMLLexer.h"
#include "camel/parse/syntax/lexer.h"

#include <iostream>
#include <random>

namespace parsediff {

namespace {

struct Item {
    std::string kind;
    size_t begin;
    size_t end;
    bool operator==(const Item &) const = default;
};

std::vector<Item> antlrItems(const std::string &text, size_t &errors) {
    // ANTLR counts code points; the new lexer counts bytes.
    std::vector<size_t> byteOf;
    for (size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            byteOf.push_back(i);
        }
    }
    byteOf.push_back(text.size());
    antlr4::ANTLRInputStream input(text);
    OpenCMLLexer lexer(&input);
    lexer.removeErrorListeners();
    antlr4::CommonTokenStream stream(&lexer);
    stream.fill();
    errors = lexer.getNumberOfSyntaxErrors();
    std::vector<Item> items;
    for (antlr4::Token *token : stream.getTokens()) {
        std::string kind;
        switch (token->getType()) {
        case antlr4::Token::EOF:
            kind = "EOF";
            break;
        case OpenCMLLexer::IDENTIFIER:
            kind = "IDENTIFIER";
            break;
        case OpenCMLLexer::INTEGER:
            kind = "INTEGER";
            break;
        case OpenCMLLexer::REAL:
            kind = "REAL";
            break;
        case OpenCMLLexer::STRING:
            kind = "STRING";
            break;
        case OpenCMLLexer::MULTI_STR:
            kind = "MULTI_STR";
            break;
        case OpenCMLLexer::FSTRING:
            kind = "FSTRING";
            break;
        case OpenCMLLexer::COMMAND:
            kind = "COMMAND";
            break;
        case OpenCMLLexer::LIN_CMT:
            kind = "LIN_CMT";
            break;
        case OpenCMLLexer::BLK_CMT:
            kind = "BLK_CMT";
            break;
        default:
            kind = token->getText();
        }
        if (token->getType() == antlr4::Token::EOF) {
            items.push_back({kind, text.size(), text.size()});
        } else {
            items.push_back(
                {kind, byteOf[token->getStartIndex()], byteOf[token->getStopIndex() + 1]});
        }
    }
    return items;
}

std::vector<Item> handItems(const std::string &text, size_t &errors, bool &unterminatedComment) {
    using namespace camel::parse::syntax;
    const TokenBuffer buffer = Lexer::tokenize(text);
    errors                   = buffer.errors().size();
    unterminatedComment      = false;
    for (const LexError &error : buffer.errors()) {
        unterminatedComment |= error.kind == LexErrorKind::UnterminatedComment;
    }
    std::vector<Item> items;
    for (const Token &token : buffer.tokens()) {
        std::string kind;
        switch (token.kind) {
        case TokenKind::Error:
            continue;
        case TokenKind::Eof:
            kind = "EOF";
            break;
        case TokenKind::Identifier:
            kind = "IDENTIFIER";
            break;
        case TokenKind::Integer:
            kind = "INTEGER";
            break;
        case TokenKind::Real:
            kind = "REAL";
            break;
        case TokenKind::String:
            kind = "STRING";
            break;
        case TokenKind::MultiStr:
            kind = "MULTI_STR";
            break;
        case TokenKind::FString:
            kind = "FSTRING";
            break;
        case TokenKind::Command:
            kind = "COMMAND";
            break;
        case TokenKind::LineComment:
            kind = "LIN_CMT";
            break;
        case TokenKind::BlockComment:
            kind = "BLK_CMT";
            break;
        default:
            kind = std::string(tokenKindText(token.kind));
            if (std::string(buffer.text(token)) != kind) {
                kind = "MISMATCHED-TEXT:" + kind;
            }
        }
        items.push_back({kind, token.begin, token.end});
    }
    return items;
}

} // namespace

int runLexDiff(const std::vector<std::string> &files) {
    size_t same = 0, different = 0, intended = 0, withErrors = 0, tokens = 0;
    for (const std::string &path : files) {
        const std::string text = readFile(path);
        size_t antlrErrors = 0, handErrors = 0;
        bool unterminatedComment = false;
        const auto a             = antlrItems(text, antlrErrors);
        const auto h             = handItems(text, handErrors, unterminatedComment);
        tokens += h.size();
        if (antlrErrors > 0 || handErrors > 0) {
            ++withErrors;
        }
        if (unterminatedComment) {
            ++intended;
            continue;
        }
        if (a == h && (antlrErrors > 0) == (handErrors > 0)) {
            ++same;
            continue;
        }
        ++different;
        size_t at = 0;
        while (at < a.size() && at < h.size() && a[at] == h[at]) {
            ++at;
        }
        std::cout << "DIFF " << path << " at token " << at;
        if (at < a.size()) {
            std::cout << " antlr=" << a[at].kind << "[" << a[at].begin << "," << a[at].end << ")";
        }
        if (at < h.size()) {
            std::cout << " hand=" << h[at].kind << "[" << h[at].begin << "," << h[at].end << ")";
        }
        std::cout << " errors antlr=" << antlrErrors << " hand=" << handErrors << "\n";
    }
    std::cout << "LEXDIFF files=" << files.size() << " same=" << same
              << " different=" << different << " intended(unterminated comment)=" << intended
              << " with-lexical-errors=" << withErrors << " tokens=" << tokens << "\n";
    return different == 0 ? 0 : 1;
}

/// Random character edits on slices of the corpus: the cases a corpus of well-formed programs
/// does not contain (odd numbers, quotes and backslashes, operators run together).
int runLexFuzz(const std::vector<std::string> &files, size_t iterations, uint64_t seed) {
    static const std::vector<std::string> kPieces = {
        "'",  "\"", "`",  "\\", "'''", "\"\"\"", "0",  "1",  "7",  "8",  "9",  ".",  "..",
        "...", "e", "E",  "x",  "b",   "0x",     "0b", "+",  "-",  "*",  "/",  "//", "/*",
        "*/", "<",  ">",  "=",  "!",   "?",      "&",  "|",  "^",  "@",  "%",  "~",  ":",
        "::", ";",  ",",  "(",  ")",   "[",      "]",  "{",  "}",  "_",  "#",  "\n", " ",
        "\t", "\r", "\f", "$",  "a",   "f",      "if", "as", "->", "=>", "\xE5\x90\x8D"};
    std::mt19937_64 rng(seed);
    auto pick       = [&](size_t bound) { return static_cast<size_t>(rng() % bound); };
    auto isContinuation = [](char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; };
    size_t different = 0, intended = 0;
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        std::string text = readFile(files[pick(files.size())]);
        if (text.size() > 240) {
            size_t from = pick(text.size() - 240);
            while (from > 0 && isContinuation(text[from])) {
                --from;
            }
            size_t to = from + 240;
            while (to < text.size() && isContinuation(text[to])) {
                ++to;
            }
            text = text.substr(from, to - from);
        }
        const size_t edits = 1 + pick(6);
        for (size_t e = 0; e < edits && !text.empty(); ++e) {
            size_t at = pick(text.size() + 1);
            while (at < text.size() && isContinuation(text[at])) {
                ++at;
            }
            if (pick(4) == 0 && at < text.size()) {
                size_t end = at + 1;
                while (end < text.size() && isContinuation(text[end])) {
                    ++end;
                }
                text.erase(at, end - at);
            } else {
                text.insert(at, kPieces[pick(kPieces.size())]);
            }
        }
        size_t antlrErrors = 0, handErrors = 0;
        bool unterminatedComment = false;
        const auto a             = antlrItems(text, antlrErrors);
        const auto h             = handItems(text, handErrors, unterminatedComment);
        if (unterminatedComment) {
            ++intended;
            continue;
        }
        if (a == h && (antlrErrors > 0) == (handErrors > 0)) {
            continue;
        }
        if (++different <= 5) {
            size_t at = 0;
            while (at < a.size() && at < h.size() && a[at] == h[at]) {
                ++at;
            }
            std::cout << "DIFF iteration " << iteration << " at token " << at;
            if (at < a.size()) {
                std::cout << " antlr=" << a[at].kind << "[" << a[at].begin << "," << a[at].end
                          << ")";
            }
            if (at < h.size()) {
                std::cout << " hand=" << h[at].kind << "[" << h[at].begin << "," << h[at].end
                          << ")";
            }
            std::cout << " errors antlr=" << antlrErrors << " hand=" << handErrors << "\n----\n"
                      << text << "\n----\n";
        }
    }
    std::cout << "LEXFUZZ iterations=" << iterations << " seed=" << seed
              << " different=" << different << " intended(unterminated comment)=" << intended
              << "\n";
    return different == 0 ? 0 : 1;
}

} // namespace parsediff
