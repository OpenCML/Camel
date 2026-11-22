// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/rule_index.h"

#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace camel::core {

namespace {

std::mutex registryMutex;

// What the reader collected for a module, before the module is built: one entry per `rule`
// declaration, keyed by the module that declares it.
std::map<std::string, std::vector<RuleObject>> collected;

// The index a ruleset module answers questions from: family -> ("head@arity" -> the rules filed
// there, in filing order). A sorted map, so a lookup is one binary search and a family's rules are
// printable in a fixed order.
std::map<std::string, std::map<std::string, std::vector<RuleObject>>> indexed;

/// The key a rule is filed under: the head of its left side and that head's operand count.
std::string keyOf(const std::string &head, uint32_t arity) {
    return head + "@" + std::to_string(arity);
}

} // namespace

void collectRuleHead(
    const std::string &module,
    std::string head,
    std::string rule,
    std::string rhsCallee,
    std::string shape,
    std::string variables,
    std::string outerUses,
    std::string condition,
    std::string parameterTypes,
    uint32_t arity,
    uint32_t tokenStart,
    uint32_t tokenEnd) {
    RuleObject object;
    object.name           = std::move(rule);
    object.head           = std::move(head);
    object.arity          = arity;
    object.rhsCallee      = std::move(rhsCallee);
    object.variables      = std::move(variables);
    object.shape          = std::move(shape);
    object.outerUses      = std::move(outerUses);
    object.condition      = std::move(condition);
    object.parameterTypes = std::move(parameterTypes);
    object.tokenStart     = tokenStart;
    object.tokenEnd       = tokenEnd;
    std::lock_guard<std::mutex> lock(registryMutex);
    collected[module].push_back(std::move(object));
}

std::vector<RuleObject> takeRuleHeads(const std::string &module) {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<RuleObject> rules;
    const auto at = collected.find(module);
    if (at != collected.end()) {
        rules = std::move(at->second);
        collected.erase(at);
    }
    return rules;
}

std::string indexRule(const std::string &family, const RuleObject &rule) {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::map<std::string, std::vector<RuleObject>> &rules = indexed[family];
    std::vector<RuleObject> &slots                        = rules[keyOf(rule.head, rule.arity)];
    for (const RuleObject &slot : slots) {
        if (slot.name == rule.name) {
            return {}; // one module built twice files its rules twice
        }
        // Two rules of one operator that cannot be told apart -- one spelling, one operand count,
        // one parameter-type list -- are a duplicate: neither can be preferred, and a family that
        // holds both would answer by filing order. The condition is *not* part of that test:
        // a `when` clause is a question about the same operands, not a different shape, so two laws
        // of one parameter-type list are one law written twice whatever their guards say. A rule
        // that wants to answer two questions of one operand writes two laws of two parameter types
        // (`jvp_float_from_float::<S>` beside `jvp_float_from_int(x: int)`,
        // `stdlib/rulesets/jvp.cml`) or one law that branches on the type.
        //
        // Anything else is two laws of one key, which is what a family is for. Two rules whose
        // parameter types differ (`(T[...], T[...])` and `(T[...], T)`, `(x: S)` and `(x: int)`)
        // answer for different operands -- `T[...]`'s `t[0]` is the open-rank array and `T[]`'s is
        // the element (design principles 5.4), and an `int` source is not a float one -- and this
        // text used to call them a duplicate because both wrote `is_float::<T>()`.
        //
        // The choice between rules that share a key is not made here: the operand types are not
        // known at this point, and the engine makes it where they are (`lookupRules`).
        if (slot.parameterTypes == rule.parameterTypes) {
            return slot.name;
        }
    }
    slots.push_back(rule);
    return {};
}

std::vector<RuleObject>
lookupRules(const std::string &family, const std::string &head, uint32_t arity) {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<RuleObject> rules;
    const auto familyAt = indexed.find(family);
    if (familyAt == indexed.end()) {
        return rules;
    }
    const auto at = familyAt->second.find(keyOf(head, arity));
    if (at == familyAt->second.end()) {
        return rules;
    }
    rules = at->second;
    return rules;
}

RuleMatch lookupRule(const std::string &family, const std::string &head, uint32_t arity) {
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto familyAt = indexed.find(family);
    if (familyAt == indexed.end()) {
        return RuleMatch{RuleLookup::NoSuchFamily, {}};
    }
    const auto at = familyAt->second.find(keyOf(head, arity));
    if (at == familyAt->second.end() || at->second.empty()) {
        return RuleMatch{RuleLookup::NoRuleForPrimitive, {}};
    }
    // One answer when the key holds one rule. A key that holds several is a family the engine
    // dispatches over, and this question -- "which rule is it" -- has no single answer: the caller
    // that has to choose asks `lookupRules` and lets the engine decide.
    if (at->second.size() > 1) {
        return RuleMatch{RuleLookup::NoRuleForPrimitive, {}};
    }
    return RuleMatch{RuleLookup::Found, at->second.front()};
}

std::optional<uint32_t> arityOfHead(const std::string &family, const std::string &head) {
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto familyAt = indexed.find(family);
    if (familyAt == indexed.end()) {
        return std::nullopt;
    }
    // The keys are `head@arity`, so the family's map is scanned for this head and the answer is
    // given only when there is exactly one: two arities of one spelling are two rules, and a caller
    // that has only the spelling would be guessing.
    const std::string prefix = head + "@";
    std::optional<uint32_t> found;
    for (const auto &[key, rules] : familyAt->second) {
        (void)rules;
        if (key.rfind(prefix, 0) != 0) {
            continue;
        }
        if (found.has_value()) {
            return std::nullopt;
        }
        found = static_cast<uint32_t>(std::stoul(key.substr(prefix.size())));
    }
    return found;
}

std::string rhsCalleeOf(const std::string &family, const std::string &rule) {
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto familyAt = indexed.find(family);
    if (familyAt == indexed.end()) {
        return {};
    }
    std::string callee;
    for (const auto &[key, rules] : familyAt->second) {
        (void)key;
        for (const RuleObject &filed : rules) {
            if (filed.name == rule) {
                callee = filed.rhsCallee;
            }
        }
    }
    return callee;
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

std::vector<RuleObject> rulesOf(const std::string &family) {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<RuleObject> rules;
    const auto at = indexed.find(family);
    if (at == indexed.end()) {
        return rules;
    }
    rules.reserve(at->second.size());
    for (const auto &[key, slots] : at->second) {
        (void)key;
        rules.insert(rules.end(), slots.begin(), slots.end());
    }
    return rules;
}

} // namespace camel::core