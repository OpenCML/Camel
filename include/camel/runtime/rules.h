// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * A rule as the front end hands it over: two functions of the same parameters (rewrite design
 * 2.1: a rule is the shorthand of a pair of functions), the left one read as the pattern and the
 * right one as what the matched value becomes. compileRule turns them into the Rules the engine
 * of writings.h works with.
 *
 * The left function's result is the pattern; its parameters are the pattern variables, in
 * order. The nodes of a pattern are operators, constants and the parameters; anything else
 * (a branch, a loop, a call of a user function) is refused. On the right, a term made only of
 * constants and `const` parameters is a computed constant: it is evaluated when the rule is
 * applied, by the operator itself (so it faults like the program would, and a rule whose
 * constant would fault is not applied).
 */

#pragma once

#include "camel/runtime/program.h"
#include "camel/runtime/writings.h"

#include <stdexcept>
#include <vector>

namespace camel::runtime {

/// The two functions of a rule, with what the declaration says about them.
struct RuleSource {
    std::string name;
    uint16_t id = 0;
    /// The numeric permissions the rule needs (`#requires(...)`).
    camel::core::sem::PermissionMask needs = camel::core::sem::kNoPermissions;
    /// `<=>`: the rule holds both ways.
    bool bidirectional = false;
    const Program *program = nullptr;
    GraphId lhs            = kInvalidGraphId;
    GraphId rhs            = kInvalidGraphId;
    /// Per parameter: `const x: T`, which only matches a constant.
    std::vector<uint8_t> constParams;
    /// The condition, if the declaration has one.
    std::function<bool(const Match &)> when;
    /// The condition's parameter count, when it has one: the sides' parameters, each `const` one
    /// followed by the hidden node parameter `node_of` reaches it through (the rule of
    /// 2026-10-23 on `rule_when_const_node`). The rule's own parameters come first, in the order
    /// it writes them; the hidden node parameters follow, one per `const` parameter, in the same
    /// order.
    uint32_t whenArity = 0;
    /// Where the hidden node parameters of the condition begin: the number of parameters of the
    /// sides, which is also the first of the hidden ones. The condition's own parameters are the
    /// first `whenNodesStart` of its parameter list, and it must take `whenNodesStart` plus one
    /// more per `const` parameter of the sides (`RuleCompileError` otherwise).
    uint32_t whenNodesStart = 0;
};

/// The rule cannot be a rule: what is wrong with it.
class RuleCompileError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

/// The Rule of the source (two for a bidirectional one: the second reads the right side as the
/// pattern). `context` evaluates the computed constants.
std::vector<Rule> compileRule(camel::core::context::Context &context, const RuleSource &source);

} // namespace camel::runtime
