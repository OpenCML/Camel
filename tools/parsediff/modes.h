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
 * What the modes of camel-parsediff share: reading the corpus and parsing one text with one of
 * the two parsers.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace parsediff {

enum class ParserChoice { Antlr, Hand };

struct ParseOutcome {
    bool syntaxOk = false; // no lexical and no syntax error
    bool accepted = false; // and the AST was built
    bool crashed  = false;
    std::string dump;        // normalized AST and diagnostics (ast_dump.h)
    std::string diagnostics; // the diagnostics alone, as JSON
};

std::string readFile(const std::string &path);
std::vector<std::string> readList(const std::string &path);
/// `lineBreaks`: rule 2 of the hand-written parser (on, except to attribute a difference).
ParseOutcome parseWith(
    ParserChoice which, const std::string &path, const std::string &text, bool lineBreaks = true);

/// `stats`: the places of a corpus that the two new rules touch, found on the ANTLR parse.
int runAntlrStats(const std::vector<std::string> &files);

/// `diff`: the ASTs the two parsers build, file by file.
int runDiff(const std::vector<std::string> &files, const std::string &expectFile);
/// The class of a pair of outcomes: same, both-reject, ast-diff, hand-rejects, hand-accepts.
std::string classify(const ParseOutcome &antlr, const ParseOutcome &hand);

/// `fuzz`: the same on mutated programs; differences are attributed and written to `outDir`.
int runFuzz(
    const std::vector<std::string> &files, size_t iterations, uint64_t seed,
    const std::string &outDir);

/// `lexdiff`: the hand-written lexer against the ANTLR lexer.
int runLexDiff(const std::vector<std::string> &files);
int runLexFuzz(const std::vector<std::string> &files, size_t iterations, uint64_t seed);

} // namespace parsediff
