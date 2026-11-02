// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Forward-mode rules: where an operator's tangent rule comes from.
 *
 * A reverse rule is registered with its computation (camel/core/derivative.h, VjpRule). A forward
 * rule is not: it is written where every rule is written -- in a ruleset module, as a `rule` whose
 * left side is the operator -- and all this file answers is *where* it is written down.
 *
 * **Nothing is registered per operator.** The key a rule is filed under is a fact about the
 * operator, so it is read off the operator: `:op/add_d` is written `+` with two operands because
 * that is what `add` means, and `tensor:matmul` is written `matmul(...)` because that is the
 * operator's own name. Three operators cannot be written that way -- `xs[i]` is a bracket form with no
 * token, the array product's `*` key at two operands is already `:op/mul_d`'s, and `ltod`'s spelling
 * `float(x)` at one operand is a key every conversion to a double shares -- and those three are
 * the whole of `ruleKeyExceptions()`, which says so in words and is printed by the `std::rulesets`
 * pass so that an exception is never silent.
 *
 * The name is read from the family's index every time it is asked for, never remembered from
 * registration time. That is what makes this an index consumer rather than a second copy of the
 * rules: remove a rule from the ruleset and the next question has no answer, which is exactly what a
 * transform must see.
 */

#pragma once

#include "camel/parse/ast/rule_head.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace camel::core {

/// The key a rule is filed under in a ruleset family: the spelling of the rule's left side, and how
/// many operands that spelling takes (`-x` and `x - w` are two keys).
struct RuleKey {
    std::string head; // the operator of the rule's left side, e.g. "+"
    uint32_t arity = 0;
};

/// An operator whose key cannot be derived from the operator itself, and why. Every one of these is
/// an operator whose rule is written with a spelling the operator does not have -- a token the
/// language gave to another operator, or no token at all -- so the key is written down here once, in
/// words, instead of being derivable. The list is printed (`ruleKeyExceptionLines`) wherever the
/// ruleset index is reported: an exception that nobody reads is the same as a wrong answer.
struct RuleKeyException {
    std::string uri;   // the operator, e.g. ":op/idx_arr"
    std::string head;  // the spelling its rule's left side is written with, e.g. "index_into"
    uint32_t arity = 0;
    std::string why; // why the key cannot be derived, in one sentence
};

/// The exceptions, in uri order.
std::span<const RuleKeyException> ruleKeyExceptions();

/// One line per exception: `uri -> head/arity because why`. What the `std::rulesets` pass prints.
std::vector<std::string> ruleKeyExceptionLines();

/// The key the operator `uri`'s rule is filed under in `family`, derived from the operator itself:
/// nullopt when the operator's name says nothing about the rule's spelling (an operator that is not
/// written with a call or a token at all, and is not one of the exceptions).
///
/// `family` is needed for the arity of a module operator: `tensor:sum_axis` is `sum_axis` at three
/// operands, and three is a fact the family's index holds (`arityOfHead`), not one the name spells.
std::optional<RuleKey> ruleKeyOf(const std::string &family, const std::string &uri);

/// What `uri`'s forward rule is called right now, read from `rulesets.jvp`'s index at this moment.
/// `nullopt` when the operator has no key at all (which is not the same as a family that lacks the
/// rule: the two are `NoRuleForPrimitive` and `NoSuchFamily` in the answer).
std::optional<camel::parse::ast::RuleMatch> forwardRuleOf(const std::string &uri);

/// `forwardRuleOf`, for the family a linear function's pullback is written in.
std::optional<camel::parse::ast::RuleMatch> transposeRuleOf(const std::string &uri);

/// `forwardRuleOf`, for the family a batching transform asks (`rulesets.vmap`).
std::optional<camel::parse::ast::RuleMatch> vmapRuleOf(const std::string &uri);

} // namespace camel::core