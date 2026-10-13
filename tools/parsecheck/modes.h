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
 * What the modes of camel-parsecheck share: reading the corpus and running the front end on
 * one text.
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace parsecheck {

struct ParseOutcome {
    bool syntaxOk = false; // no lexical and no syntax error
    bool accepted = false; // and the AST was built
    std::string dump;        // normalized AST and diagnostics (ast_dump.h)
    std::string diagnostics; // the diagnostics alone, as JSON
    std::string structure;   // the AST without positions (empty unless accepted)
    std::string formatted;   // the text camel-format gives (empty unless syntaxOk)
};

std::string readFile(const std::string &path);
std::vector<std::string> readList(const std::string &path);
ParseOutcome parseText(const std::string &path, const std::string &text);

/// `hashes`: a hash of the normalized AST of every file, the regression snapshot of the parser.
int runHashes(const std::vector<std::string> &files);
/// `fmtcheck`: formatting keeps the AST, and formatting twice changes nothing.
int runFormatCheck(const std::vector<std::string> &files);
/// `fuzz`: the parser on mutated programs; a mutant that breaks an invariant goes to `outDir`.
int runFuzz(
    const std::vector<std::string> &files, size_t iterations, uint64_t seed,
    const std::string &outDir);
/// `bench`: the time of each step of the front end on one file.
int runBench(const std::string &path, size_t rounds);

} // namespace parsecheck
