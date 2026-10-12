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
 * `camel-parsediff fuzz`: the two parsers on mutated programs.
 *
 * A seed is a file of the corpus, cut into tokens with the white space before each. A mutant
 * is the seed after one to three of: delete a token, duplicate one, swap two neighbours, insert
 * a token from a vocabulary of every operator and keyword, cut the file at a token, splice the
 * head of one file onto the tail of another, replace the white space before a token (nothing, a
 * space, a line break). Both parsers read the mutant; the outcomes are compared as in `diff`.
 *
 * A difference is then attributed, automatically where that is possible:
 *
 *   rule2    the hand-written parser with rule 2 switched off agrees with ANTLR: the
 *            difference is the line-break rule
 *   rule1    with rule 2 off they still differ, and the mutant has a `<` or `>` that the two
 *            readings can differ on: an angle bracket that touches the token before it, or a
 *            `>` inside an angle-bracket list
 *   raise, minus   the mutant uses `raise` as a name, or has a minus separated from the number
 *            after it: the two rulings the ANTLR grammar does not implement
 *   other    none of these: a defect of one of the parsers, or an ambiguity the ANTLR parser
 *            resolves by prediction. Every such mutant is written to the output directory.
 *
 * `rule1` is a superset of what rule 1 causes (ANTLR's reading of adjacent angle brackets
 * depends on prediction and cannot be reproduced); a sample of it is written out as well.
 */

#include "modes.h"

#include "camel/parse/syntax/lexer.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>

#include <csetjmp>
#include <csignal>

namespace parsediff {

namespace {

using camel::parse::syntax::Lexer;
using camel::parse::syntax::Token;
using camel::parse::syntax::TokenBuffer;
using camel::parse::syntax::TokenKind;

struct Piece {
    std::string space; // what stands before the token
    std::string text;
};

std::vector<Piece> piecesOf(const std::string &text) {
    const TokenBuffer buffer = Lexer::tokenize(text);
    std::vector<Piece> pieces;
    uint32_t end = 0;
    for (const Token &token : buffer.tokens()) {
        if (token.kind == TokenKind::Eof) {
            break;
        }
        pieces.push_back(
            {text.substr(end, token.begin - end), text.substr(token.begin, token.length())});
        end = token.end;
    }
    return pieces;
}

// The ANTLR-based AST builder dereferences missing children on some inputs it never met in a
// test (`import {} from a`). The harness has to survive that: a fault while the ANTLR side runs
// is counted and the mutant skipped. (The memory of that parse is leaked; this is a harness.)
sigjmp_buf g_antlrFault;
void onAntlrFault(int) { siglongjmp(g_antlrFault, 1); }

std::string join(const std::vector<Piece> &pieces) {
    std::string out;
    for (const Piece &piece : pieces) {
        out += piece.space;
        out += piece.text;
    }
    out += "\n";
    return out;
}

/// Whether the two readings of angle brackets can differ on this text.
bool angleSensitive(const std::string &text) {
    const TokenBuffer buffer = Lexer::tokenize(text);
    int angleDepth           = 0;
    for (const Token &token : buffer.tokens()) {
        if (token.kind == TokenKind::LAngle) {
            return true;
        }
        if (token.kind == TokenKind::Lt) {
            ++angleDepth; // may open a list for ANTLR
        }
        if (token.kind == TokenKind::Gt && angleDepth > 0) {
            return true;
        }
    }
    return false;
}

/// The two rulings of 2026-10-12 the ANTLR grammar does not have: `raise` is a name, and a
/// minus is the sign of a number only when it touches it.
std::string rulingOn(const std::string &text) {
    const TokenBuffer buffer = Lexer::tokenize(text);
    TokenKind previous       = TokenKind::Eof;
    bool minus               = false;
    for (const Token &token : buffer.tokens()) {
        if (camel::parse::syntax::isTrivia(token.kind)) {
            continue;
        }
        if (token.kind == TokenKind::Identifier && buffer.text(token) == "raise") {
            return "raise";
        }
        if ((token.kind == TokenKind::Integer || token.kind == TokenKind::Real) &&
            previous == TokenKind::Minus && token.spaceBefore()) {
            minus = true;
        }
        previous = token.kind;
    }
    return minus ? "minus" : "";
}

} // namespace

int runFuzz(
    const std::vector<std::string> &files, size_t iterations, uint64_t seed,
    const std::string &outDir) {
    static const std::vector<std::string> kVocabulary = [] {
        std::vector<std::string> words = {"x", "f", "T", "0", "1", "2.5", "'s'", "_", "main"};
        for (const auto &info : camel::parse::syntax::keywordTable()) {
            words.emplace_back(info.text);
        }
        for (size_t k = static_cast<size_t>(TokenKind::Semi);
             k < camel::parse::syntax::kTokenKindCount;
             ++k) {
            words.emplace_back(camel::parse::syntax::tokenKindText(static_cast<TokenKind>(k)));
        }
        return words;
    }();
    static const std::vector<std::string> kSpaces = {"", "", " ", " ", "\n", "\n    ", " // c\n"};

    // Seeds: small files parse fast under ANTLR's full-LL fallback, which most mutants reach.
    std::vector<std::vector<Piece>> seeds;
    for (const std::string &path : files) {
        const std::string text = readFile(path);
        if (text.size() <= 3000) {
            seeds.push_back(piecesOf(text));
        }
    }
    if (seeds.empty()) {
        std::cerr << "no seed files\n";
        return 2;
    }
    std::filesystem::create_directories(outDir);
    std::mt19937_64 rng(seed);
    auto pick = [&](size_t bound) { return bound == 0 ? 0 : static_cast<size_t>(rng() % bound); };

    std::map<std::string, size_t> counts;
    size_t written = 0;
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        std::vector<Piece> pieces = seeds[pick(seeds.size())];
        const size_t mutations    = 1 + pick(3);
        for (size_t m = 0; m < mutations && !pieces.empty(); ++m) {
            const size_t at = pick(pieces.size());
            switch (pick(8)) {
            case 0:
                pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(at));
                break;
            case 1:
            {
                const Piece copy = pieces[at];
                pieces.insert(pieces.begin() + static_cast<std::ptrdiff_t>(at), copy);
                break;
            }
            case 2:
                if (at + 1 < pieces.size()) {
                    std::swap(pieces[at].text, pieces[at + 1].text);
                }
                break;
            case 3:
                pieces.insert(
                    pieces.begin() + static_cast<std::ptrdiff_t>(at),
                    Piece{kSpaces[pick(kSpaces.size())], kVocabulary[pick(kVocabulary.size())]});
                break;
            case 4:
                pieces.resize(at);
                break;
            case 5: {
                const std::vector<Piece> &other = seeds[pick(seeds.size())];
                pieces.resize(at);
                const size_t from = pick(other.size());
                pieces.insert(
                    pieces.end(),
                    other.begin() + static_cast<std::ptrdiff_t>(from),
                    other.end());
                break;
            }
            default:
                pieces[at].space = kSpaces[pick(kSpaces.size())];
                break;
            }
        }
        const std::string text = join(pieces);
        // If the hand-written side crashes, the mutant is in this file.
        { std::ofstream(outDir + "/current.cml", std::ios::binary) << text; }
        ParseOutcome a;
        std::signal(SIGSEGV, onAntlrFault);
        if (sigsetjmp(g_antlrFault, 1) == 0) {
            a = parseWith(ParserChoice::Antlr, "fuzz.cml", text);
            std::signal(SIGSEGV, SIG_DFL);
        } else {
            std::signal(SIGSEGV, SIG_DFL);
            ++counts["antlr-crash"];
            continue;
        }
        const ParseOutcome h = parseWith(ParserChoice::Hand, "fuzz.cml", text);
        std::string kind       = classify(a, h);
        ++counts[a.syntaxOk ? "antlr-accepts" : "antlr-rejects"];
        if (kind == "same" || kind == "both-reject") {
            ++counts[kind];
            continue;
        }
        std::string cause = "other";
        if (kind != "crash") {
            const ParseOutcome legacy = parseWith(ParserChoice::Hand, "fuzz.cml", text, false);
            const std::string again   = classify(a, legacy);
            if (again == "same" || again == "both-reject") {
                cause = "rule2";
            } else if (angleSensitive(text)) {
                cause = "rule1";
            } else if (const std::string ruling = rulingOn(text); !ruling.empty()) {
                cause = ruling;
            }
        }
        const size_t n = ++counts[cause + ":" + kind];
        if (cause == "other" || (cause == "rule1" && n <= 40) || (cause == "rule2" && n <= 10)) {
            const std::string name =
                outDir + "/" + cause + "_" + kind + "_" + std::to_string(iteration) + ".cml";
            std::ofstream(name, std::ios::binary) << text;
            ++written;
        }
    }
    std::cout << "FUZZ iterations=" << iterations << " seed=" << seed << " seeds=" << seeds.size();
    size_t other = 0;
    for (const auto &[kind, count] : counts) {
        std::cout << " " << kind << "=" << count;
        if (kind.rfind("other:", 0) == 0) {
            other += count;
        }
    }
    std::cout << " written=" << written << " dir=" << outDir << "\n";
    return other == 0 ? 0 : 1;
}

} // namespace parsediff
