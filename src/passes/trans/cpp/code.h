// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The statements of a generated C++ function before they are printed: lines and nested
 * if / else-if / else chains. A function is lowered into this form first, so that what is
 * decided for the whole body (whether it is a loop because of a tail call) is known before
 * any text is written.
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace camel::passes::cpp {

struct Stmt;
using Block = std::vector<Stmt>;

struct Stmt {
    enum class Kind {
        Line,   // one line of C++ (a declaration, an expression statement, return, continue)
        Branch, // if (c0) { b0 } else if (c1) { b1 } ... else { bn }
        Scope,  // { b } (keeps the temporaries of a tail call to themselves)
        Loop,   // for (;;) { b } (a loop of the program; `break` leaves it)
    };

    Kind kind = Kind::Line;
    std::string text;
    /// Branch: (condition, block) pairs; an empty condition is the final `else`.
    std::vector<std::pair<std::string, Block>> arms;
    /// Scope, Loop: the statements in it.
    Block body;

    static Stmt line(std::string text) { return Stmt{Kind::Line, std::move(text), {}, {}}; }
    static Stmt scope(Block body) { return Stmt{Kind::Scope, {}, {}, std::move(body)}; }
    static Stmt loop(Block body) { return Stmt{Kind::Loop, {}, {}, std::move(body)}; }
};

/// Writes `block` with `indent` levels of four spaces.
void printBlock(std::string &out, const Block &block, int indent);

} // namespace camel::passes::cpp
