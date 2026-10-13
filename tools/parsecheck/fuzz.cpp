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
 * `camel-parsecheck fuzz` and `bench`.
 *
 * fuzz. A seed is a file of the corpus cut into tokens, each with the white space before it. A
 * mutant is the seed after one to three of: delete a token, duplicate one, swap two neighbours,
 * insert a token from a vocabulary of every operator and keyword, cut the file at a token,
 * splice the head of one file onto the tail of another, replace the white space before a token
 * (nothing, a space, a line break, a comment). Most mutants are wrong programs, which is the
 * point: the parser must return a tree for every text. Checked on each mutant:
 *
 *   - the front end returns (a crash leaves the mutant in <dir>/current.cml);
 *   - the tree is lossless: its token leaves, in order, are exactly the tokens of the buffer
 *     that are not comments, each once, and every node spans its first to its last leaf;
 *   - a tree without Error nodes and without recorded errors is what "parsed" means, and the
 *     AST builder accepts every such tree without throwing;
 *   - an accepted mutant, formatted, is accepted again.
 *
 * (While the ANTLR parser existed this mode also compared the two parsers on every mutant; the
 * results are in docs/technical/34_handwritten_parser.md.)
 *
 * bench. Per round: the lexer, lexer and parser, the whole front end to the AST. Minimum and
 * median over the rounds, in microseconds.
 */

#include "modes.h"

#include "camel/parse/parse.h"
#include "camel/parse/syntax/lexer.h"
#include "camel/parse/syntax/parser.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <sstream>

namespace parsecheck {

namespace {

using namespace camel::parse::syntax;

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

std::string join(const std::vector<Piece> &pieces) {
    std::string out;
    for (const Piece &piece : pieces) {
        out += piece.space;
        out += piece.text;
    }
    out += "\n";
    return out;
}

/// Appends the token leaves of `node` to `leaves`; false if a node's span is not that of its
/// leaves. Sets `errorNodes` when the subtree holds an Error node.
bool collect(Node node, std::vector<uint32_t> &leaves, bool &errorNodes) {
    if (node.kind() == NodeKind::Error) {
        errorNodes = true;
    }
    const size_t first = leaves.size();
    for (size_t i = 0; i < node.childCount(); ++i) {
        const Child child = node.child(i);
        if (child.isToken()) {
            leaves.push_back(child.token().index());
        } else if (!collect(child.node(), leaves, errorNodes)) {
            return false;
        }
    }
    if (leaves.size() == first) {
        return node.empty(); // a node without tokens (something missing in a wrong text)
    }
    if (node.empty()) {
        return false;
    }
    return node.firstToken().index() == leaves[first] && node.lastToken().index() == leaves.back();
}

/// What is wrong with the tree of `text`, or empty.
std::string violation(const std::string &text) {
    const Tree tree = Parser::parse(text);
    if (!tree.root()) {
        return "no root";
    }
    std::vector<uint32_t> leaves;
    bool errorNodes = false;
    if (!collect(tree.root(), leaves, errorNodes)) {
        return "a node does not span its leaves";
    }
    std::vector<uint32_t> expected;
    for (uint32_t i = 0; i < tree.tokens().size(); ++i) {
        if (!isTrivia(tree.tokens()[i].kind)) {
            expected.push_back(i);
        }
    }
    if (leaves != expected) {
        return "the leaves of the tree are not the tokens of the text";
    }
    if (errorNodes && tree.errors().empty()) {
        return "an Error node without a recorded error";
    }
    return "";
}

template <typename F> double microseconds(F &&f) {
    const auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
        .count();
}

} // namespace

int runFuzz(
    const std::vector<std::string> &files, size_t iterations, uint64_t seed,
    const std::string &outDir) {
    static const std::vector<std::string> kVocabulary = [] {
        std::vector<std::string> words = {"x", "f", "T", "0", "1", "2.5", "'s'", "_", "main"};
        for (const auto &info : keywordTable()) {
            words.emplace_back(info.text);
        }
        for (size_t k = static_cast<size_t>(TokenKind::Semi); k < kTokenKindCount; ++k) {
            words.emplace_back(tokenKindText(static_cast<TokenKind>(k)));
        }
        return words;
    }();
    static const std::vector<std::string> kSpaces = {"", "", " ", " ", "\n", "\n    ", " // c\n"};

    std::vector<std::vector<Piece>> seeds;
    for (const std::string &path : files) {
        const std::string text = readFile(path);
        if (text.size() <= 6000) {
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
    size_t failures = 0;
    auto fail = [&](size_t iteration, const std::string &what, const std::string &text) {
        ++failures;
        const std::string name = outDir + "/fail_" + std::to_string(iteration) + ".cml";
        std::ofstream(name, std::ios::binary) << text;
        std::cout << "FAIL iteration " << iteration << ": " << what << " (" << name << ")\n";
    };
    for (size_t iteration = 0; iteration < iterations; ++iteration) {
        std::vector<Piece> pieces = seeds[pick(seeds.size())];
        const size_t mutations    = 1 + pick(3);
        for (size_t m = 0; m < mutations && !pieces.empty(); ++m) {
            const size_t at = pick(pieces.size());
            switch (pick(8)) {
            case 0:
                pieces.erase(pieces.begin() + static_cast<std::ptrdiff_t>(at));
                break;
            case 1: {
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
        { std::ofstream(outDir + "/current.cml", std::ios::binary) << text; }
        if (const std::string what = violation(text); !what.empty()) {
            fail(iteration, what, text);
            continue;
        }
        ParseOutcome outcome;
        try {
            outcome = parseText("fuzz.cml", text);
        } catch (const std::exception &e) {
            fail(iteration, std::string("exception: ") + e.what(), text);
            continue;
        }
        ++counts[outcome.syntaxOk ? "parsed" : "rejected"];
        if (outcome.syntaxOk) {
            const ParseOutcome again = parseText("fuzz.cml", outcome.formatted);
            if (!again.syntaxOk) {
                fail(iteration, "the formatted text does not parse", text);
            }
        }
    }
    std::filesystem::remove(outDir + "/current.cml");
    std::cout << "FUZZ iterations=" << iterations << " seed=" << seed << " seeds=" << seeds.size();
    for (const auto &[kind, count] : counts) {
        std::cout << " " << kind << "=" << count;
    }
    std::cout << " failures=" << failures << "\n";
    return failures == 0 ? 0 : 1;
}

int runBench(const std::string &path, size_t rounds) {
    const std::string text = readFile(path);
    std::vector<double> lex, cst, ast;
    size_t tokens = 0, nodes = 0;
    for (size_t round = 0; round < rounds; ++round) {
        lex.push_back(microseconds([&] { tokens = Lexer::tokenize(text).size(); }));
        cst.push_back(microseconds([&] { nodes = Parser::parse(text).nodeCount(); }));
        ast.push_back(microseconds([&] {
            auto sourceContext = std::make_shared<camel::source::SourceContext>();
            auto diagnostics =
                std::make_shared<camel::core::error::Diagnostics>("main", path, sourceContext);
            camel::parse::CamelParser parser(diagnostics);
            parser.setSourceContext(sourceContext);
            std::istringstream input(text);
            if (!parser.parse(input)) {
                std::cerr << "bench: the file is not accepted\n";
                std::exit(1);
            }
        }));
    }
    const size_t lines = static_cast<size_t>(std::count(text.begin(), text.end(), '\n'));
    std::cout << "BENCH " << path << " bytes=" << text.size() << " lines=" << lines
              << " tokens=" << tokens << " cst-nodes=" << nodes << " rounds=" << rounds << "\n";
    auto report = [](const char *name, std::vector<double> &values) {
        std::sort(values.begin(), values.end());
        std::cout << "  " << name << ": min=" << static_cast<long>(values.front())
                  << "us median=" << static_cast<long>(values[values.size() / 2]) << "us\n";
    };
    report("lex", lex);
    report("lex+cst", cst);
    report("lex+cst+ast", ast);
    return 0;
}

} // namespace parsecheck
