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

/// The operator a rule's left side is written with. Three shapes are read: `a <op> b` gives
/// `<op>`, `<op> a` gives `<op>`, and `f(a, b)` gives `f`. `shape` is set, and the answer is
/// empty, when the left side is none of them (a literal, a tuple, an operator chain at one
/// level): the caller reports it, and the shape's name says what was written.
std::string leftHeadOf(syntax::Node left, std::string &shape);

/// What a ruleset's index holds for one rule: the head of its left side and the rule's name.
struct RuleHead {
    std::string head;  // the operator the left side is written with, e.g. "+"
    std::string rule;  // the rule's name
    std::string shape; // when the left side is not one operator: the shape's name, else empty
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
    uint32_t tokenStart,
    uint32_t tokenEnd);

/// The rules collected for `module`, removed by the read: one index per ruleset module, taken
/// once where the module is built.
std::vector<RuleHead> takeRuleHeads(const std::string &module);

} // namespace camel::parse::ast