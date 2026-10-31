// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/parse/ast/rule_head.h"

#include "camel/parse/syntax/cst.h"

#include <cstdlib>
#include <map>
#include <mutex>

namespace camel::parse::ast {

namespace {

std::mutex registryMutex;
std::map<std::string, std::vector<RuleHead>> registry;
// The index a ruleset module answers questions from: family -> ("head@arity" -> rule name). A
// sorted map, so a lookup is one binary search and a family's rules are printable in a fixed order.
std::map<std::string, std::map<std::string, std::string>> indexed;
// family -> rule name -> the function the rule's right side calls (`add_tangent`, ...). Kept by
// rule name because that is what a lookup answers with: a transform reads the rule, then asks what
// it calls.
std::map<std::string, std::map<std::string, std::string>> callees;

} // namespace

std::string leftHeadOf(syntax::Node left, std::string &shape, uint32_t &arity) {
    shape.clear();
    arity = 0;
    if (!left) {
        shape = "nothing";
        return {};
    }
    // Down through the layers that hold one expression: the expression grammar has a level per
    // precedence, so `x + w` reaches the operator only after the single-child layers above it
    // (assign, logical or, ...) are stepped through.
    syntax::Node current = left;
    while (current && current->childCount() == 1) {
        const syntax::Child only = current->child(0);
        if (!only->isNode()) {
            break;
        }
        current = only->node();
    }
    if (!current) {
        shape = "nothing";
        return {};
    }
    // The operator: the one token among the children of a binary or prefix node (`a + b` has
    // three children, `-a` two), and the callee's name for a call, whose children are the
    // callee and the argument list and no operator token at all.
    size_t tokenCount = 0;
    size_t nodeCount  = 0;
    syntax::TokenRef operatorToken;
    syntax::Node firstNode;
    syntax::Node secondNode;
    for (size_t index = 0; index < current->childCount(); ++index) {
        const syntax::Child child = current->child(index);
        if (child->isToken()) {
            ++tokenCount;
            operatorToken = child->token();
        } else if (child->isNode()) {
            if (nodeCount == 0) {
                firstNode = child->node();
            } else if (nodeCount == 1) {
                secondNode = child->node();
            }
            ++nodeCount;
        }
    }
    if (tokenCount == 1 && nodeCount >= 1) {
        // How many operands the operator was given, not how it is spelled: `-a` has one and
        // `a - b` has two, and they are `__neg__` and `__sub__` to the compiler.
        arity = static_cast<uint32_t>(nodeCount);
        return operatorToken.text();
    }
    if (tokenCount == 0 && nodeCount >= 2 && firstNode) {
        // `f(a, b)` is filed under `f` with one operand per argument: the second child is the
        // argument list, and its own children are the arguments.
        arity = secondNode && secondNode->childCount() > 0
                    ? static_cast<uint32_t>(secondNode->childCount())
                    : static_cast<uint32_t>(nodeCount - 1);
        return firstNode->text();
    }
    shape = std::string(syntax::nodeKindName(current->kind()));
    return {};
}

namespace {

/// The first call inside `node`, as the name it calls: a node with no operator token of its own
/// and at least two children, the callee and its argument list (`f(a, b)`). The right side of a
/// rule is `(dx, dw) => add_tangent(x, w, dx, dw)`, so this finds `add_tangent`.
std::string firstCallIn(syntax::Node node) {
    if (!node) {
        return {};
    }
    size_t tokens = 0;
    size_t nodes  = 0;
    syntax::Node first;
    syntax::Node second;
    for (size_t index = 0; index < node->childCount(); ++index) {
        const syntax::Child child = node->child(index);
        if (child->isToken()) {
            ++tokens;
        } else if (child->isNode()) {
            if (nodes == 0) {
                first = child->node();
            } else if (nodes == 1) {
                second = child->node();
            }
            ++nodes;
        }
    }
    if (tokens == 0 && nodes >= 2 && first && second &&
        std::string_view(syntax::nodeKindName(second->kind())) == "ParentArgues") {
        return first->text();
    }
    // The layers above an expression hold one child each for the precedence levels; a lambda
    // holds its parameters and its body. Descend into every child and take the first answer.
    for (size_t index = 0; index < node->childCount(); ++index) {
        const syntax::Child child = node->child(index);
        if (!child->isNode()) {
            continue;
        }
        std::string found = firstCallIn(child->node());
        if (!found.empty()) {
            return found;
        }
    }
    return {};
}

} // namespace

std::string rightCalleeOf(syntax::Node right) {
    return firstCallIn(right);
}

void collectRuleHead(
    const std::string &module,
    std::string head,
    std::string rule,
    std::string rhsCallee,
    std::string shape,
    uint32_t arity,
    uint32_t tokenStart,
    uint32_t tokenEnd) {
    std::lock_guard<std::mutex> lock(registryMutex);
    registry[module].push_back(RuleHead{
        std::move(head),
        std::move(rule),
        std::move(rhsCallee),
        std::move(shape),
        arity,
        tokenStart,
        tokenEnd});
}

std::vector<RuleHead> takeRuleHeads(const std::string &module) {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<RuleHead> heads;
    const auto at = registry.find(module);
    if (at != registry.end()) {
        heads = std::move(at->second);
        registry.erase(at);
    }
    return heads;
}

std::string indexRule(const std::string &family, const RuleHead &rule) {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::map<std::string, std::string> &rules = indexed[family];
    const std::string key = rule.head + "@" + std::to_string(rule.arity);
    const auto at         = rules.find(key);
    if (at == rules.end()) {
        rules.emplace(key, rule.rule);
        callees[family][rule.rule] = rule.rhsCallee;
        return {};
    }
    if (at->second == rule.rule) {
        return {};
    }
    return at->second;
}

RuleMatch lookupRule(const std::string &family, const std::string &head, uint32_t arity) {
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto familyAt = indexed.find(family);
    if (familyAt == indexed.end()) {
        return RuleMatch{RuleLookup::NoSuchFamily, {}};
    }
    const auto at = familyAt->second.find(head + "@" + std::to_string(arity));
    if (at == familyAt->second.end()) {
        return RuleMatch{RuleLookup::NoRuleForPrimitive, {}};
    }
    return RuleMatch{RuleLookup::Found, at->second};
}

std::string rhsCalleeOf(const std::string &family, const std::string &rule) {
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto at = callees.find(family);
    if (at == callees.end()) {
        return {};
    }
    const auto ruleAt = at->second.find(rule);
    return ruleAt == at->second.end() ? std::string() : ruleAt->second;
}

std::vector<std::string> ruleFamilies() {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<std::string> families;
    families.reserve(indexed.size());
    for (const auto &[family, rules] : indexed) {
        (void)rules;
        families.push_back(family);
    }
    return families;
}

std::vector<RuleEntry> rulesOf(const std::string &family) {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<RuleEntry> entries;
    const auto at = indexed.find(family);
    if (at == indexed.end()) {
        return entries;
    }
    entries.reserve(at->second.size());
    for (const auto &[key, rule] : at->second) {
        const size_t at2 = key.rfind('@');
        if (at2 == std::string::npos) {
            continue;
        }
        entries.push_back(RuleEntry{
            key.substr(0, at2),
            static_cast<uint32_t>(std::strtoul(key.c_str() + at2 + 1, nullptr, 10)),
            rule});
    }
    return entries;
}

} // namespace camel::parse::ast