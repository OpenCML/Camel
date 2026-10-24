// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * camel-parsecheck: the checks of the front end that work on a corpus of source files.
 *
 *   camel-parsecheck dump <file>                 the normalized AST of one file
 *   camel-parsecheck hashes <list>               a hash of that per file: the regression snapshot
 *   camel-parsecheck fmtcheck <list>             formatting keeps the AST and is stable
 *   camel-parsecheck fuzz <list> <n> <seed> <dir>   the parser on mutated programs
 *   camel-parsecheck bench <file> <rounds>       time of lexer, parser, AST builder
 *
 * <list> is a text file with one source path per line (test/tools/parsecheck/corpus.py writes
 * one). Everything runs in-process. The tool began as the differential harness against the
 * ANTLR parser (docs/technical/34_handwritten_parser.md, section 9); what is left is what needs
 * no second parser.
 */

#include "ast_dump.h"
#include "modes.h"

#include "../format/fmt.h"
#include "camel/init.h"
#include "camel/parse/parse.h"

#include <exception>
#include <fstream>
#include <iostream>
#include <sstream>

using namespace camel::core::error;

namespace parsecheck {

std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    // Git may check the corpus out with CRLF on Windows. Snapshots include byte offsets
    // and string contents, so use the committed LF representation on every platform.
    const std::string text = buffer.str();
    std::string normalized;
    normalized.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            continue;
        }
        normalized += text[i];
    }
    return normalized;
}

std::vector<std::string> readList(const std::string &path) {
    std::vector<std::string> files;
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            files.push_back(line);
        }
    }
    return files;
}

ParseOutcome parseText(const std::string &path, const std::string &text) {
    ParseOutcome outcome;
    auto sourceContext = std::make_shared<camel::source::SourceContext>();
    auto diagnostics   = std::make_shared<Diagnostics>("main", path, sourceContext);
    camel::parse::CamelParser parser(diagnostics);
    parser.setSourceContext(sourceContext);
    std::istringstream input(text);
    outcome.accepted = parser.parse(input);
    outcome.syntaxOk = parser.parsed();
    // Ranges are resolved once, for both forms of the report: the JSON one a person reads, and the
    // identity one the snapshot hashes.
    parser.diagnostics()->fetchAll(parser.tokenPositions());
    std::ostringstream diagText;
    parser.dumpDiagnostics(diagText, true);
    outcome.diagnostics = diagText.str();
    // What the snapshot records about a rejected file: the code and the place of every diagnostic,
    // never its wording. A message, a help text or a note is prose and prose is edited; an edit to
    // one must not move test/plans/feat/parse/golden/ast_hashes.txt (H252).
    std::ostringstream identity;
    parser.diagnostics()->dumpIdentity(identity);
    outcome.dump = dumpParse(outcome.accepted, parser.ast(), sourceContext, identity.str());
    if (outcome.accepted) {
        std::ostringstream structure;
        dumpAstNode(structure, parser.ast(), sourceContext, 0, false);
        outcome.structure = structure.str();
    }
    if (parser.parsed()) {
        Formatter formatter(*parser.tree(), Formatter::Options{});
        outcome.formatted = formatter.format();
        // A comment the formatter could not place: it is written at the end of the file rather than
        // dropped, and this counts them so that a run which had to do that is not read as a pass.
        outcome.unplaced = formatter.unplacedCommentCount();
        // The comment tokens as the formatter has to write them back. The text is the source
        // spelling, which is what insertComment copies; a comment the formatter drops is a
        // comment this list holds and `formatted` does not (snapshot.cpp).
        const syn::TokenBuffer &buffer = parser.tree()->tokens();
        for (const syn::Token &token : buffer.tokens()) {
            if (token.kind == TK::LineComment || token.kind == TK::DocComment ||
                token.kind == TK::BlockComment) {
                outcome.comments.emplace_back(buffer.text(token));
            }
        }
    }
    return outcome;
}

} // namespace parsecheck

namespace {

int usage() {
    std::cerr << "usage: camel-parsecheck dump <file>\n"
                 "       camel-parsecheck hashes <list>\n"
                 "       camel-parsecheck fmtcheck <list>\n"
                 "       camel-parsecheck fuzz <list> <iterations> <seed> <dir>\n"
                 "       camel-parsecheck bench <file> <rounds>\n";
    return 2;
}

} // namespace

int main(int argc, char **argv) {
    // Where libcamel is looked for (on Windows it is loaded on first use, from ../libs).
    camel::ScopedRuntime camelRuntime;
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        return usage();
    }
    const std::string mode = args[0];
    if (mode == "dump" && args.size() == 2) {
        try {
            std::cout << parsecheck::parseText(args[1], parsecheck::readFile(args[1])).dump;
            return 0;
        } catch (const std::exception &e) {
            // Name the file that the front end could not read, and fail: a crash would say
            // nothing about which file it was.
            std::cerr << "error " << args[1] << ": " << e.what() << "\n";
            return 1;
        }
    }
    if (mode == "hashes" && args.size() == 2) {
        return parsecheck::runHashes(parsecheck::readList(args[1]));
    }
    if (mode == "fmtcheck" && args.size() == 2) {
        return parsecheck::runFormatCheck(parsecheck::readList(args[1]));
    }
    if (mode == "fuzz" && args.size() == 5) {
        return parsecheck::runFuzz(
            parsecheck::readList(args[1]),
            std::stoull(args[2]),
            std::stoull(args[3]),
            args[4]);
    }
    if (mode == "bench" && args.size() == 3) {
        return parsecheck::runBench(args[1], std::stoull(args[2]));
    }
    return usage();
}
