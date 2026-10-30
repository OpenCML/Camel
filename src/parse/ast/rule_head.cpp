// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/parse/ast/rule_head.h"

#include "camel/parse/syntax/cst.h"

#include <map>
#include <mutex>

namespace camel::parse::ast {

namespace {

std::mutex registryMutex;
std::map<std::string, std::vector<RuleHead>> registry;

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

void collectRuleHead(
    const std::string &module,
    std::string head,
    std::string rule,
    std::string shape,
    uint32_t arity,
    uint32_t tokenStart,
    uint32_t tokenEnd) {
    std::lock_guard<std::mutex> lock(registryMutex);
    registry[module].push_back(RuleHead{
        std::move(head), std::move(rule), std::move(shape), arity, tokenStart, tokenEnd});
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

} // namespace camel::parse::ast