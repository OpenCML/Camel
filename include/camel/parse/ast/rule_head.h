// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Reading a rule's head off its declaration, while the declaration is still syntax.
 *
 * A rule is written as two functions of the same parameters -- `a + b => add_tangent(a, b)` -- and
 * the few facts a transform needs about it cannot be read from the graphs those functions become:
 * a rule's `__lhs` has no body at the stage a module's declarations are read, and by the time a
 * transform asks, the expression that spelled the rule is gone. So they are read here, at the only
 * place they exist, and handed to the kernel's index (`camel/core/rule_index.h`), which files them
 * as a `RuleObject` the compiler compiled rather than as a question about a syntax tree.
 *
 * This file is the parser's half and only the parser's: every function takes a `syntax::Node`, and
 * nothing outside `camel/parse/` includes it. The store and the questions asked of it are the
 * kernel's (`camel/core/rule_index.h`); a transform never sees a syntax node.
 */

#pragma once

#include <cstdint>
#include <string>

namespace camel::parse::syntax {
class Node;
}

namespace camel::parse::ast {

/// The operator a rule's left side is written with, and how many operands it takes. Three shapes
/// are read: `a <op> b` gives `<op>` and 2, `<op> a` gives `<op>` and 1, and `f(a, b)` gives `f` and
/// the number of arguments. The operand count is part of what a rule is filed under, not a detail of
/// the spelling: `-x` and `x - w` are two primitives (`__neg__` and `__sub__`) that share one token,
/// so an index keyed by the token alone would file them as one. `shape` is set, and the answer is
/// empty, when the left side is none of the three (a literal, a tuple, an operator chain at one
/// level): the caller reports it, and the shape's name says what was written.
std::string leftHeadOf(syntax::Node left, std::string &shape, uint32_t &arity);

/// The function a rule's right side calls, e.g. `add_tangent` for `x + w => (dx, dw) =>
/// add_tangent(x, w, dx, dw)`, or empty when the right side calls nothing (it is a value).
///
/// A transform that replaces an operator with the rule's meaning has to call that function, and it
/// is written in the rule -- so the name is read here, where the declaration is still syntax, and
/// filed with the rule.
std::string rightCalleeOf(syntax::Node right);

/// The names the rule's left side binds, in the order it writes them (`a / b` gives `a,b`), joined
/// by commas. Its two sides are graphs over one tuple -- `lhsPorts=1` against `rhsPorts=1` -- and
/// the tuple is exactly these names in this order, so a transform that has to assemble it reads
/// the list the rule was filed with. Empty for a left side that binds nothing.
std::string variableNamesOf(syntax::Node left);

/// The names the left side binds that the right side *also writes* (`x` for the right side
/// `(bx, bw) => vmap_add(bx, x)`), joined by commas in the order the right side writes them; empty
/// when the right side reads none of them.
///
/// This is what a transform that reads a rule's callee and never calls the rule's right side as a
/// function has to know: nothing binds the pattern's names in such a graph, so a right side that
/// reads one is a rule no such transform can use. The module build refuses it where it is written
/// (`SemanticDiag::RuleRightUsesOuterVariable`); this answers the question the refusal asks.
std::string outerNamesUsedInRight(syntax::Node left, syntax::Node right);

} // namespace camel::parse::ast