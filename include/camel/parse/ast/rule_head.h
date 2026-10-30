// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <string>
#include <vector>

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

/// What a ruleset's index holds for one rule: the head of its left side, its operand count, and the
/// rule's name.
struct RuleHead {
    std::string head;  // the operator the left side is written with, e.g. "+"
    std::string rule;  // the rule's name
    std::string shape; // when the left side is not one operator: the shape's name, else empty
    uint32_t arity = 0; // how many operands the head takes: 2 for `a + b`, 1 for `-a`
    // The rule's tokens in its module's buffer, for a diagnostic about the rule itself.
    uint32_t tokenStart = 0;
    uint32_t tokenEnd   = 0;
};

/// Records one rule of a ruleset while its declarations are read. The module is named by the
/// caller: the index is keyed by module, never by an ambient "current module".
void collectRuleHead(
    const std::string &module,
    std::string head,
    std::string rule,
    std::string shape,
    uint32_t arity,
    uint32_t tokenStart,
    uint32_t tokenEnd);

/// The rules collected for `module`, removed by the read: one index per ruleset module, taken
/// once where the module is built.
std::vector<RuleHead> takeRuleHeads(const std::string &module);

/// Why a lookup of a rule answered the way it did.
enum class RuleLookup {
    Found,              // the family has a rule for that operator with that many operands
    NoSuchFamily,       // no ruleset module of that name was built into this program
    NoRuleForPrimitive, // the family is there and holds no rule for that operator and count
};

/// What `lookupRule` answers: the rule's name when there is one, and why there is not otherwise.
struct RuleMatch {
    RuleLookup status = RuleLookup::NoSuchFamily;
    std::string rule; // the rule's name, when `status` is Found; empty otherwise
};

/// Files `rule` in `family`'s index under the head of its left side and that head's operand count.
/// The name of a *different* rule already filed under that key is returned -- the caller reports the
/// duplicate, and the two are never silently merged -- and an empty string means the rule was filed,
/// or was already there under the same name (a module built twice files its rules twice).
std::string indexRule(const std::string &family, const RuleHead &rule);

/// The rule of `family` whose left side is `head` with `arity` operands. A ruleset that is only
/// built and printed answers nothing, and reads as if it did: this is the question a transform asks
/// it, and the answer says which of the two ways there is no rule.
RuleMatch lookupRule(const std::string &family, const std::string &head, uint32_t arity);

/// The families built so far, in name order: what a reader can ask `lookupRule` about.
std::vector<std::string> ruleFamilies();

} // namespace camel::parse::ast