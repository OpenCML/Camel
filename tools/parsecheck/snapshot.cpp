// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Two checks of the hand-written front end that need no second parser.
 *
 * `camel-parsecheck hashes <list>` prints, per file, a 64-bit hash of what the front end makes of
 * it: the normalized AST with every node's token range and origin, and the diagnostics, or the
 * diagnostics alone when the file is rejected. The list committed as
 * test/plans/feat/parse/golden/ast_hashes.txt is the regression snapshot of the parser: it was
 * recorded when the ASTs were last compared with those of the ANTLR parser, just before that
 * parser was removed.
 *
 * `camel-parsecheck fmtcheck <list>` formats every file that parses and checks that the result
 * parses to the same AST (positions aside), that every comment of the input is in it, and that
 * formatting it again changes nothing. The first is what makes the formatter safe under the two
 * layout rules: where it breaks a line or removes a space, the meaning must not move. A comment
 * carries no meaning to the AST, so the second stands in for it: losing a comment is losing source
 * text, and a formatter that does it is not safe to run in a gate. The third is the
 * property that makes the gate stable: a second pass that differs would flip it red and green on
 * the same input.
 */

#include "modes.h"

#include <algorithm>
#include <exception>
#include <iomanip>
#include <iostream>

namespace parsecheck {

namespace {

uint64_t fnv1a(const std::string &text) {
    uint64_t hash = 1469598103934665603ull;
    for (unsigned char c : text) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return hash;
}

/// One comment of a file, clipped to one line, for the report of a comment the formatter dropped.
std::string oneLine(const std::string &comment) {
    const size_t end = comment.find('\n');
    const std::string first = comment.substr(0, end);
    return first.size() > 80 ? first.substr(0, 80) + "..." : first;
}

/// The formatter orders the imports by module; the order of imports means nothing.
std::string withSortedImports(const std::string &structure) {
    std::vector<std::string> lines;
    size_t at = 0;
    while (at < structure.size()) {
        const size_t end = structure.find('\n', at);
        lines.push_back(structure.substr(at, end - at));
        at = end == std::string::npos ? structure.size() : end + 1;
    }
    auto first = lines.begin();
    while (first != lines.end() && first->rfind("  ImportDecl", 0) != 0) {
        ++first;
    }
    auto last = first;
    while (last != lines.end() && last->rfind("  ImportDecl", 0) == 0) {
        ++last;
    }
    std::sort(first, last);
    std::string out;
    for (const std::string &line : lines) {
        out += line;
        out += '\n';
    }
    return out;
}

} // namespace

int runHashes(const std::vector<std::string> &files) {
    // A list that names nothing would hash nothing and report success; that is not a pass
    // (agents/test.md, H185/H199).
    if (files.empty()) {
        std::cerr << "hashes: expected at least 1 file in the list, got 0; refusing to hash nothing\n";
        return 2;
    }
    int failures = 0;
    for (const std::string &path : files) {
        // The diagnostics name the file; the hash must not depend on where the file lies.
        try {
            const ParseOutcome outcome = parseText("file.cml", readFile(path));
            std::cout << std::hex << std::setw(16) << std::setfill('0') << fnv1a(outcome.dump)
                      << std::dec << " " << (outcome.accepted ? "ok " : "no ") << path << "\n";
        } catch (const std::exception &e) {
            // A file the front end cannot read is a failure of the check, not of the tool: name
            // the file and go on with the rest, so one file does not hide the others.
            std::cout << "error " << path << ": " << e.what() << "\n";
            ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

int runFormatCheck(const std::vector<std::string> &files) {
    // Every file below counts as checked or skipped, so an empty list would print a clean summary
    // of nothing (H199). The floor that compares against the tree's parse corpus is in check.py,
    // which is the only caller that can count it; this refuses the empty list itself.
    if (files.empty()) {
        std::cerr << "fmtcheck: expected at least 1 file in the list, got 0; refusing to check nothing\n";
        return 2;
    }
    size_t checked = 0, skipped = 0, changed = 0, unstable = 0, broken = 0, comments = 0,
           dropped = 0;
    for (const std::string &path : files) {
        try {
            const ParseOutcome original = parseText(path, readFile(path));
            if (!original.accepted || original.formatted.empty()) {
                ++skipped;
                continue;
            }
            ++checked;
            // Every comment of the input must appear in the text formatting writes. A comment is
            // not a node of the AST, so the structural comparison below cannot see one that was
            // dropped: a formatter that loses a comment rewrites the source silently, and a gate
            // must not report that as a pass. Checked before the second parse, so a file whose
            // formatting does not parse back still gets its comment verdict.
            for (const std::string &comment : original.comments) {
                ++comments;
                if (original.formatted.find(comment) == std::string::npos) {
                    ++dropped;
                    std::cout << "DROPPED " << path << " " << oneLine(comment) << "\n";
                }
            }
            const ParseOutcome again = parseText(path, original.formatted);
            if (!again.accepted ||
                withSortedImports(again.structure) != withSortedImports(original.structure)) {
                ++changed;
                std::cout << "CHANGED " << path
                          << (again.accepted ? " (another AST)" : " (does not parse)") << "\n";
                if (!again.accepted) {
                    std::cout << "    " << again.diagnostics.substr(0, 400) << "\n";
                }
                continue;
            }
            if (again.formatted != original.formatted) {
                ++unstable;
                std::cout << "UNSTABLE " << path << "\n";
            }
        } catch (const std::exception &e) {
            ++broken;
            std::cout << "BROKEN " << path << ": " << e.what() << "\n";
        }
    }
    std::cout << "FMTCHECK files=" << files.size() << " checked=" << checked
              << " skipped(not accepted)=" << skipped << " changed=" << changed
              << " unstable=" << unstable << " broken=" << broken << " comments=" << comments
              << " dropped=" << dropped << "\n";
    // `comments` is reported but not gated here: the floor that refuses a run which examined no
    // comment is in check.py, which can count the tree's corpus (as it does for the file list).
    return changed == 0 && dropped == 0 && broken == 0 ? 0 : 1;
}

} // namespace parsecheck
