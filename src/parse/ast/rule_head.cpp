// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/parse/ast/rule_head.h"

#include "camel/parse/syntax/cst.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace camel::parse::ast {

namespace {

std::mutex registryMutex;
std::map<std::string, std::vector<RuleHead>> registry;
// The index a ruleset module answers questions from: family -> ("head@arity" -> the rules filed
// there, in filing order). A sorted map, so a lookup is one binary search and a family's rules are
// printable in a fixed order.
//
// A key holds **one or more** rules. An operator whose spelling several operators share -- `float`
// at one operand is `float(x: float)`, `float(x: int)`, `float(x: string)` and the rest -- has one
// rule per operand type, and *which* of them answers is decided by the engine, which instantiates a
// rule at the operand's own type and refuses by name when there is no instance. The index answers
// names; the type is not its to encode.
// One rule filed under a key: its name, and the condition it answers by. The condition is part of
// what a slot is, because two rules of one operator are a family the engine dispatches over
// (`lookupRules`) -- and two with the same condition are a duplicate, since nothing could choose
// between them.
struct RuleSlot {
    std::string rule;
    std::string condition;
    std::string parameterTypes;
};

std::map<std::string, std::map<std::string, std::vector<RuleSlot>>> indexed;
// family -> rule name -> the function the rule's right side calls (`add_tangent`, ...). Kept by
// rule name because that is what a lookup answers with: a transform reads the rule, then asks what
// it calls.
std::map<std::string, std::map<std::string, std::string>> callees;
// family -> rule name -> the names its left side binds (`a,b`). Kept beside the callee, read from
// the same declaration: the tuple both sides of the rule take.
std::map<std::string, std::map<std::string, std::string>> variables;

/// The arguments a call was given: the nodes of its argument list, without the commas. `(a, b)` is a
/// `ParentArgues` holding an `ArgumentList` holding an `IndexValues`, and the commas are tokens at
/// the level that holds the arguments -- so counting the children of the list counts the commas too
/// and `f(a, b)` comes out as three operands. Descend to the level that holds the arguments and count
/// its nodes.
uint32_t argumentCountOf(syntax::Node argues) {
    syntax::Node list = argues;
    for (;;) {
        syntax::Node inner;
        for (size_t index = 0; index < list->childCount() && !inner; ++index) {
            const syntax::Child child = list->child(index);
            if (!child->isNode()) {
                continue;
            }
            const std::string_view kind = syntax::nodeKindName(child->node()->kind());
            if (kind == "ArgumentList" || kind == "IndexValues") {
                inner = child->node();
            }
        }
        if (!inner) {
            break;
        }
        list = inner;
    }
    uint32_t count = 0;
    for (size_t index = 0; index < list->childCount(); ++index) {
        if (list->child(index)->isNode()) {
            ++count;
        }
    }
    return count;
}

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
    syntax::Node argues;
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
            if (std::string_view(syntax::nodeKindName(child->node()->kind())) == "ParentArgues") {
                argues = child->node();
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
    if (tokenCount == 0 && firstNode && argues) {
        // `f(a, b)` is filed under `f` with one operand per argument: the argument list is the
        // `ParentArgues` child, and its arguments are what it holds, not the commas between them. The
        // head is the callee written before it, whether or not it also names its own types
        // (`f::<T>(a, b)`: the `TypeArguments` child sits between the callee and the arguments).
        arity = argumentCountOf(argues);
        return firstNode->text();
    }
    if (tokenCount == 0 && nodeCount >= 2 && firstNode) {
        // No `ParentArgues` was found: a two-node node with no operator token. Nothing in the tree is
        // filed this way today; the count descends into an argument list the way a call's does, so a
        // shape that reaches here is filed by its arguments rather than by its apostrophes.
        arity = argumentCountOf(secondNode);
        return firstNode->text();
    }
    shape = std::string(syntax::nodeKindName(current->kind()));
    return {};
}

namespace {

/// The first call inside `node`, as the name it calls: a node with no operator token of its own
/// and at least two children, the callee and its argument list (`f(a, b)`). The right side of a
/// rule is `(dx, dw) => add_tangent(x, w, dx, dw)`, so this finds `add_tangent`. A call may also
/// name its own type arguments -- `(bx: Lift<T>) => mul_batch::<T>(bx)` -- and then the arguments are
/// the third child, behind the `TypeArguments` the callee carries: the name to call is still the
/// callee, and a rule whose right side needs an explicit type argument is a rule like any other.
std::string firstCallIn(syntax::Node node) {
    if (!node) {
        return {};
    }
    size_t nodes = 0;
    syntax::Node first;
    syntax::Node second;
    for (size_t index = 0; index < node->childCount(); ++index) {
        const syntax::Child child = node->child(index);
        if (!child->isNode()) {
            continue;
        }
        if (nodes == 0) {
            first = child->node();
        } else if (nodes == 1) {
            second = child->node();
        }
        ++nodes;
    }
    if (nodes >= 2 && first && second) {
        // The callee is the first child and the arguments (or the type arguments the callee carries
        // before them, `f::<T>(a)`) the second. No token requirement: a callee that names its own
        // types writes its `::` `<` and `>` among the children of the same node, so requiring none
        // would read exactly the calls this case is here for as "calls nothing".
        const std::string_view kind = syntax::nodeKindName(second->kind());
        if (kind == "ParentArgues" || kind == "TypeArguments") {
            return first->text();
        }
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

/// The names the left side binds, collected in order: a leaf that is written as an identifier.
/// The layers above an expression hold one child each (the precedence levels), so they are stepped
/// through; an operator node is stepped into for its operands; a leaf is where a name is written.
void collectNames(syntax::Node node, std::vector<std::string> &names) {
    if (!node) {
        return;
    }
    size_t tokens = 0;
    std::vector<syntax::Node> children;
    for (size_t index = 0; index < node->childCount(); ++index) {
        const syntax::Child child = node->child(index);
        if (child->isToken()) {
            ++tokens;
        } else if (child->isNode()) {
            children.push_back(child->node());
        }
    }
    if (tokens == 0 && children.size() == 1) {
        collectNames(children.front(), names);
        return;
    }
    if (children.empty()) {
        const std::string text = node->text();
        if (!text.empty() &&
            (std::isalpha(static_cast<unsigned char>(text.front())) || text.front() == '_')) {
            if (std::find(names.begin(), names.end(), text) == names.end()) {
                names.push_back(text);
            }
        }
        return;
    }
    for (const syntax::Node &child : children) {
        collectNames(child, names);
    }
}

} // namespace

std::string rightCalleeOf(syntax::Node right) {
    return firstCallIn(right);
}

std::string variableNamesOf(syntax::Node left) {
    std::vector<std::string> names;
    collectNames(left, names);
    std::string joined;
    for (const std::string &name : names) {
        if (!joined.empty()) {
            joined += ',';
        }
        joined += name;
    }
    return joined;
}

std::string outerNamesUsedInRight(syntax::Node left, syntax::Node right) {
    // The same leaf walk on both sides: the left answers which names the pattern binds, the right
    // which names it writes, and what both answer is the dependency the refusal is about. The right
    // side's own parameters (`bx`, `bw`), its type names and the functions it calls are not the
    // left's names, so they do not appear in the answer.
    std::vector<std::string> bound;
    collectNames(left, bound);
    if (bound.empty()) {
        return {};
    }
    std::vector<std::string> written;
    collectNames(right, written);
    std::string used;
    for (const std::string &name : written) {
        if (std::find(bound.begin(), bound.end(), name) == bound.end()) {
            continue;
        }
        if (!used.empty()) {
            used += ',';
        }
        used += name;
    }
    return used;
}

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
    std::lock_guard<std::mutex> lock(registryMutex);
    registry[module].push_back(RuleHead{
        std::move(head),
        std::move(rule),
        std::move(rhsCallee),
        std::move(shape),
        std::move(variables),
        std::move(outerUses),
        std::move(condition),
        std::move(parameterTypes),
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
    std::map<std::string, std::vector<RuleSlot>> &rules = indexed[family];
    const std::string key = rule.head + "@" + std::to_string(rule.arity);
    std::vector<RuleSlot> &slots = rules[key];
    for (const RuleSlot &slot : slots) {
        if (slot.rule == rule.rule) {
            return {}; // one module built twice files its rules twice
        }
        // Two rules of one operator that cannot be told apart -- one spelling, one operand count,
        // one condition, one parameter-type list -- are a duplicate: neither can be preferred, and
        // a family that holds both would answer by filing order.
        //
        // Anything else is two laws of one key, which is what a family is for. Two rules whose
        // parameter types differ (`(T[..], T[..])` and `(T[..], T)`) answer for different operands
        // whatever their `when` clauses say -- `T[..]`'s `t[0]` is the open-rank array and `T[]`'s
        // is the element (design principles 5.4), so the two are two laws of two shapes, and this
        // text used to call them a duplicate because both wrote `is_float::<T>()`. Two rules whose
        // parameter types are the same list but whose conditions differ are two questions the
        // engine asks of one operand (`float(x: S)` from a float is `float(dx)` and from a step is
        // `0.0`, `stdlib/rulesets/jvp.cml`), and neither is the other's duplicate either. The
        // parameter types come first because they are what the *declaration* fixes; the condition
        // stays because it is the only thing that tells those two conversions apart.
        //
        // The choice between rules that share a key is not made here: the operand types are not
        // known at this point, and the engine makes it where they are (`lookupRules`).
        if (slot.condition == rule.condition && slot.parameterTypes == rule.parameterTypes) {
            return slot.rule;
        }
    }
    slots.push_back(RuleSlot{rule.rule, rule.condition, rule.parameterTypes});
    callees[family][rule.rule]   = rule.rhsCallee;
    variables[family][rule.rule] = rule.variables;
    return {};
}

std::vector<std::string>
lookupRules(const std::string &family, const std::string &head, uint32_t arity) {
    std::lock_guard<std::mutex> lock(registryMutex);
    std::vector<std::string> names;
    const auto familyAt = indexed.find(family);
    if (familyAt == indexed.end()) {
        return names;
    }
    const auto at = familyAt->second.find(head + "@" + std::to_string(arity));
    if (at == familyAt->second.end()) {
        return names;
    }
    names.reserve(at->second.size());
    for (const RuleSlot &slot : at->second) {
        names.push_back(slot.rule);
    }
    return names;
}

RuleMatch lookupRule(const std::string &family, const std::string &head, uint32_t arity) {
    std::lock_guard<std::mutex> lock(registryMutex);
    const auto familyAt = indexed.find(family);
    if (familyAt == indexed.end()) {
        return RuleMatch{RuleLookup::NoSuchFamily, {}};
    }
    const auto at = familyAt->second.find(head + "@" + std::to_string(arity));
    if (at == familyAt->second.end() || at->second.empty()) {
        return RuleMatch{RuleLookup::NoRuleForPrimitive, {}};
    }
    // One answer when the key holds one rule. A key that holds several is a family the engine
    // dispatches over, and this question -- "which rule is it" -- has no single answer: the caller
    // that has to choose asks `lookupRules` and lets the engine decide.
    if (at->second.size() > 1) {
        return RuleMatch{RuleLookup::NoRuleForPrimitive, {}};
    }
    return RuleMatch{RuleLookup::Found, at->second.front().rule};
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
    for (const auto &[key, rule] : familyAt->second) {
        (void)rule;
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
    for (const auto &[key, slots] : at->second) {
        const size_t at2 = key.rfind('@');
        if (at2 == std::string::npos) {
            continue;
        }
        const auto familyVars = variables.find(family);
        for (const RuleSlot &slot : slots) {
        const std::string &rule = slot.rule;
        std::string names;
        if (familyVars != variables.end()) {
            const auto at3 = familyVars->second.find(rule);
            if (at3 != familyVars->second.end()) {
                names = at3->second;
            }
        }
        entries.push_back(RuleEntry{
            key.substr(0, at2),
            static_cast<uint32_t>(std::strtoul(key.c_str() + at2 + 1, nullptr, 10)),
            rule,
            names});
        }
    }
    return entries;
}

} // namespace camel::parse::ast
