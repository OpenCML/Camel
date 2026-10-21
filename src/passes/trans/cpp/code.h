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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
