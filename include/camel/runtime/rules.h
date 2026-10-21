/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */


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
    /// The numeric permissions the rule needs (`#requires<...>`).
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
    /// The condition's parameter count, when it has one; must equal the sides'.
    uint32_t whenArity = 0;
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
