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

/// One line per bound operator, sorted by uri: `uri family head/arity -> rule | no rule | no such
/// family`. The reading a test takes of what the bindings resolve to without asking, in the order a
/// reader needs it: what was bound, and what the index answers for it.
std::vector<std::string> forwardRuleDescriptions();

} // namespace camel::core