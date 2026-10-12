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
 * camel-parsediff: the differential harness of the parser replacement.
 *
 *   camel-parsediff dump  --parser antlr|hand <file>      the normalized AST of one file
 *   camel-parsediff stats <list>                           what rules 1 and 2 would change,
 *                                                          measured on the ANTLR parse
 *   camel-parsediff lexdiff <list>                         the two lexers, token for token
 *   camel-parsediff lexfuzz <list> <iterations> <seed>     the same on random character edits
 *
 * <list> is a text file with one source path per line. Every mode is in-process: a corpus of
 * some thousand files is a few seconds.
 */

#include "ast_dump.h"
#include "modes.h"

#include "camel/parse/parse.h"

#include <fstream>
#include <iostream>
#include <sstream>

using namespace camel::core::error;

namespace parsediff {

std::string readFile(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::stringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
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

ParseOutcome parseWith(ParserChoice which, const std::string &path, const std::string &text) {
    ParseOutcome outcome;
    auto sourceContext = std::make_shared<camel::source::SourceContext>();
    auto diagnostics   = std::make_shared<Diagnostics>("main", path, sourceContext);
    camel::parse::CamelParser parser(diagnostics);
    parser.setSourceContext(sourceContext);
    (void)which;
    std::istringstream input(text);
    try {
        outcome.accepted = parser.parse(input);
    } catch (const std::exception &e) {
        outcome.crashed = true;
        outcome.dump    = std::string("EXCEPTION ") + e.what() + "\n";
        return outcome;
    }
    std::ostringstream diagText;
    parser.dumpDiagnostics(diagText, true);
    outcome.diagnostics = diagText.str();
    outcome.dump = dumpParse(outcome.accepted, parser.ast(), sourceContext, outcome.diagnostics);
    return outcome;
}

} // namespace parsediff

namespace {

int usage() {
    std::cerr << "usage: camel-parsediff dump --parser antlr|hand <file>\n"
                 "       camel-parsediff stats <list>\n";
    return 2;
}

} // namespace

int main(int argc, char **argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        return usage();
    }
    const std::string mode = args[0];
    if (mode == "dump" && args.size() == 4 && args[1] == "--parser") {
        const auto which = args[2] == "hand" ? parsediff::ParserChoice::Hand
                                             : parsediff::ParserChoice::Antlr;
        std::cout << parsediff::parseWith(which, args[3], parsediff::readFile(args[3])).dump;
        return 0;
    }
    if (mode == "stats" && args.size() == 2) {
        return parsediff::runAntlrStats(parsediff::readList(args[1]));
    }
    if (mode == "lexdiff" && args.size() == 2) {
        return parsediff::runLexDiff(parsediff::readList(args[1]));
    }
    if (mode == "lexfuzz" && args.size() == 4) {
        return parsediff::runLexFuzz(
            parsediff::readList(args[1]),
            std::stoull(args[2]),
            std::stoull(args[3]));
    }
    return usage();
}
