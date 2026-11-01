// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Forward-mode rules: where an operator's tangent rule comes from.
 *
 * A reverse rule is registered with its computation (camel/core/derivative.h, VjpRule). A forward
 * rule is not: it is written where every rule is written -- in a ruleset module, as a `rule` whose
 * left side is the operator -- and what the core records here is only *where* it is written down:
 * the ruleset family and the key (`head`, `arity`) the index files it under.
 *
 * So the name is read from that family's index every time it is asked for, never remembered from
 * registration time. That is what makes the binding an index consumer rather than a second copy of
 * the rules: remove a rule from the ruleset and the next question has no answer, which is exactly
 * what a transform must see. A registry that remembered the name would keep answering "jvp_neg"
 * for a rule the program no longer has.
 */

#pragma once

#include "camel/parse/ast/rule_head.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace camel::core {

/// A ruleset module that holds a forward rule, and the key it is filed under there: the operator of
/// the rule's left side and how many operands that operator takes (`-x` and `x - w` are two keys).
struct ForwardRuleBinding {
    std::string uri;    // the operator the rule forwards, e.g. ":op/add_d"
    std::string family; // the ruleset module, e.g. "rulesets.jvp"
    std::string head;   // the operator of the rule's left side, e.g. "+"
    uint32_t arity = 0;
};

/// Binds `uri`'s forward rule to the key it is filed under in a ruleset family. Binding again
/// replaces the earlier binding: one key per operator.
void bindForwardRule(ForwardRuleBinding binding);

/// What `uri`'s forward rule is called right now, read from the family's index at this moment.
/// `nullopt` when no forward rule is bound for `uri` at all (the operator was never given one,
/// which is not the same as a ruleset that lacks the rule).
std::optional<camel::parse::ast::RuleMatch> forwardRuleOf(const std::string &uri);

/// The binding for `uri` as it stands: the family it is filed in, the head of its left side and the
/// operands that head takes. `forwardRuleOf` answers what *one* family's index resolves for `uri`;
/// this answers the key itself, which is what a second family reads -- `lookupRule("rulesets.vmap",
/// head, arity)`. The key is a property of the operator (which token, how many operands), not of the
/// family that happens to own the binding, so a family other than the bound one reads it here.
std::optional<ForwardRuleBinding> forwardBindingOf(const std::string &uri);

/// One line per bound operator, sorted by uri: `uri family head/arity -> rule | no rule | no such
/// family`. The reading a test takes of what the bindings resolve to without asking, in the order a
/// reader needs it: what was bound, and what the index answers for it.
std::vector<std::string> forwardRuleDescriptions();

/// Binds `uri`'s transpose rule to the key it is filed under in the transpose family. It is a
/// second table rather than a second field, because these are two facts about one operator: the
/// rule for forward mode and the rule for a linear function's transpose live in two rulesets (a
/// module serves one family), and neither replaces the other.
void bindTransposeRule(ForwardRuleBinding binding);

/// Binds `uri`'s batching rule to the key it is filed under in `rulesets.vmap`. A third table, for
/// the reason the transpose bindings are a second one: an operator has one rule per family (a
/// forward rule in `rulesets.jvp`, a batching rule in `rulesets.vmap`), and the two are written by
/// different hands -- so a binding may never replace the other fact, and a family that reads one
/// key cannot be made to read the other's.
///
/// The key is the operator's, not the family's: which spelling its rule's left side writes and how
/// many operands that spelling takes. A batching transform reads it here and asks its own family's
/// index with it (`lookupRule("rulesets.vmap", head, arity)`), because the spellings a rule can be
/// filed under are those of an expression (`matmul(x, w)` is `matmul` at two operands, `x @ w` is
/// `@` at two, `sum_axis(xs, axis, keepdims)` is `sum_axis` at three), not of the URI the node
/// holds. One primitive spelled two ways is two keys, so the ruleset and this table must name the
/// same one.
void bindVmapRule(ForwardRuleBinding binding);

/// What `uri`'s transpose rule is called right now, read from the transpose family's index at this
/// moment -- `forwardRuleOf`'s reading, for the family a linear function's pullback is written in.
std::optional<camel::parse::ast::RuleMatch> transposeRuleOf(const std::string &uri);

/// The transpose bindings, in the words `forwardRuleDescriptions` writes them.
std::vector<std::string> transposeRuleDescriptions();

/// The binding for `uri` in the batching family: the family the rule is written in and the key it
/// is filed under there. `nullopt` when the operator was never given a batching rule key, which is
/// not the same fact as a ruleset that lacks the rule: an operator whose key is recorded and whose
/// family holds no rule for it is a hole, and one with no key was never given a batching rule at
/// all. The batching transform reports the two apart, because the repairs differ.
std::optional<ForwardRuleBinding> vmapBindingOf(const std::string &uri);

} // namespace camel::core