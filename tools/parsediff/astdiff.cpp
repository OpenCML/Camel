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
 * `camel-parsediff diff`: the two parsers on a corpus, file by file.
 *
 * A file falls into one of these classes:
 *
 *   same            both parsers accept it and build the same AST (ast_dump.h) with the same
 *                   diagnostics, or both find it syntactically correct and the AST builder
 *                   rejects it with the same diagnostics
 *   both-reject     both report a lexical or a syntax error (the wording may differ; how often
 *                   it does is counted)
 *   ast-diff        both accept it, the trees or the diagnostics differ
 *   hand-rejects    only the ANTLR parser accepts it
 *   hand-accepts    only the hand-written parser accepts it
 *
 * Everything but `same` and `both-reject` is listed, with the first line that differs. A file
 * of expected differences (`--expect`, lines of `<class> <path>`) turns the listed ones into
 * `expected`; the exit code is 0 when nothing else is left.
 */

#include "modes.h"

#include <iostream>
#include <map>
#include <set>
#include <sstream>

namespace parsediff {

namespace {

std::string firstDifference(const std::string &a, const std::string &b) {
    std::istringstream as(a), bs(b);
    std::string al, bl;
    size_t line = 0;
    while (true) {
        const bool ae = !std::getline(as, al);
        const bool be = !std::getline(bs, bl);
        ++line;
        if (ae && be) {
            return "";
        }
        if (ae || be || al != bl) {
            std::ostringstream os;
            os << "    line " << line << "\n      antlr: " << (ae ? "<end>" : al.substr(0, 300))
               << "\n      hand:  " << (be ? "<end>" : bl.substr(0, 300));
            return os.str();
        }
    }
}

} // namespace

std::string classify(const ParseOutcome &antlr, const ParseOutcome &hand) {
    if (antlr.crashed || hand.crashed) {
        return "crash";
    }
    if (antlr.syntaxOk != hand.syntaxOk) {
        return hand.syntaxOk ? "hand-accepts" : "hand-rejects";
    }
    if (!antlr.syntaxOk) {
        return "both-reject";
    }
    return antlr.dump == hand.dump ? "same" : "ast-diff";
}

int runDiff(const std::vector<std::string> &files, const std::string &expectFile) {
    std::set<std::string> expected;
    if (!expectFile.empty()) {
        for (const std::string &line : readList(expectFile)) {
            if (line[0] != '#') {
                expected.insert(line);
            }
        }
    }
    std::map<std::string, size_t> counts;
    size_t sameRejectDiagnostics = 0, unexpected = 0;
    std::set<std::string> seenExpected;
    for (const std::string &path : files) {
        const std::string text = readFile(path);
        const ParseOutcome a   = parseWith(ParserChoice::Antlr, path, text);
        const ParseOutcome h   = parseWith(ParserChoice::Hand, path, text);
        const std::string kind = classify(a, h);
        if (kind == "same") {
            ++counts[kind];
            continue;
        }
        if (kind == "both-reject") {
            ++counts[kind];
            if (a.diagnostics == h.diagnostics) {
                ++sameRejectDiagnostics;
            }
            continue;
        }
        const std::string key = kind + " " + path;
        if (expected.contains(key)) {
            ++counts["expected:" + kind];
            seenExpected.insert(key);
            continue;
        }
        ++counts[kind];
        ++unexpected;
        std::cout << key << "\n";
        if (kind == "ast-diff") {
            std::cout << firstDifference(a.dump, h.dump) << "\n";
        } else if (kind == "crash") {
            std::cout << "    antlr: " << (a.crashed ? a.dump : "ok") << "    hand: "
                      << (h.crashed ? h.dump : "ok") << "\n";
        } else {
            std::cout << "    " << (kind == "hand-rejects" ? h.diagnostics : a.diagnostics).substr(0, 600)
                      << "\n";
        }
    }
    for (const std::string &key : expected) {
        if (!seenExpected.contains(key)) {
            std::cout << "stale expectation: " << key << "\n";
            ++unexpected;
        }
    }
    std::cout << "DIFF files=" << files.size();
    for (const auto &[kind, count] : counts) {
        std::cout << " " << kind << "=" << count;
    }
    std::cout << " (both-reject with identical diagnostics=" << sameRejectDiagnostics << ")"
              << " unexpected=" << unexpected << "\n";
    return unexpected == 0 ? 0 : 1;
}

} // namespace parsediff
