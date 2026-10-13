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
    std::ostringstream diagText;
    parser.dumpDiagnostics(diagText, true);
    outcome.diagnostics = diagText.str();
    outcome.dump = dumpParse(outcome.accepted, parser.ast(), sourceContext, outcome.diagnostics);
    if (outcome.accepted) {
        std::ostringstream structure;
        dumpAstNode(structure, parser.ast(), sourceContext, 0, false);
        outcome.structure = structure.str();
    }
    if (parser.parsed()) {
        outcome.formatted = Formatter(*parser.tree(), Formatter::Options{}).format();
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
    camel::ScopedRuntime camelRuntime;
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        return usage();
    }
    const std::string mode = args[0];
    if (mode == "dump" && args.size() == 2) {
        std::cout << parsecheck::parseText(args[1], parsecheck::readFile(args[1])).dump;
        return 0;
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
