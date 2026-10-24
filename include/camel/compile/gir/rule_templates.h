// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The templates of a generic rule, and the instances made from them.
 *
 * A rule whose head declares type parameters (`rule add_zero::<T>(x: T) { x + 0 => x }`) has no
 * function values to put into a rule: its sides are generic functions, and a generic function is
 * instantiated where it is called, not given a value (design principles 5.5). Such a rule carries
 * its templates instead, and the engine instantiates them where the rule matches: one instance per
 * type tuple, kept for the life of the context, so a second application at the same types reuses
 * the first (the ruling of 2026-10-23 on `generic-rule-instantiation`).
 *
 * This header is the compile side's record of that. The rewrite module reads it through the
 * context, which holds the table opaquely; what a template is stays private to
 * `src/compile/gir/builder.h`.
 */

#pragma once

#include "types.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace camel::core::context {
class Context;
}

namespace camel::core::type {
class Type;
}

namespace camel::runtime {
class Program;
}

namespace camel::compile::gir {

/// What came of instantiating one generic rule at one type tuple.
struct GenericRuleInstance {
    /// The tuple, in the order of the rule's type parameters.
    std::vector<camel::core::type::Type *> types;
    /// The tuple as the library writes it (`int`, `i32`, `Tensor<f32, [3]>`), for the reports.
    std::string typeText;
    /// False when no instance could be made, with `why` saying why. A failed instantiation is
    /// silent: nothing of what the compiler said about the types it was tried with is reported
    /// (the ruling of 2026-10-23 on `generic-rule-instantiation-set-and-predicate`).
    bool made = false;
    std::string why;
    /// The instance itself, when it was made: its two sides and its condition, in one program of
    /// the context that is never released. The graphs are the instance's own, so two instances of
    /// one rule never share a writing.
    const camel::runtime::Program *program = nullptr;
    const camel::runtime::GCGraph *lhs   = nullptr;
    const camel::runtime::GCGraph *rhs   = nullptr;
    /// The condition graph, null when the rule has none.
    const camel::runtime::GCGraph *when  = nullptr;
    /// The number of parameters of the condition, which is the arity of its node list.
    uint32_t whenArity = 0;
};

/// The templates of one generic rule, and one entry per type tuple tried on it.
struct GenericRuleEntry {
    /// The rule's name, as the declaration writes it.
    std::string name;
    /// The type parameters of the rule's head, in the order the head writes them (`<T>`,
    /// `<T, U>`): the arity of a tuple an instance is made at, and the names a condition's type
    /// arguments are bound to. Empty when the entry is not a generic rule's.
    std::vector<std::string> typeParams;
    /// The templates: `GenericTemplate`s of `name__lhs`, `name__rhs`, and `name__when` when the
    /// rule has a condition (null otherwise). Opaque here; the compile side owns their type.
    std::shared_ptr<void> lhs;
    std::shared_ptr<void> rhs;
    std::shared_ptr<void> when;
    /// The builder that holds the templates: an instance is built where the rule was declared,
    /// never in the program that matches it.
    void *owner = nullptr;
    /// Every tuple tried, in the order it was asked for (`genericRuleInstance` appends and
    /// answers the place of one; the vector grows, so no caller keeps a reference into it).
    std::vector<GenericRuleInstance> instances;
    /// The engine's compiled rules, one per instance that was made, owned by the engine.
    std::shared_ptr<void> compiled;
};

/// What a rule's condition says about one type tuple at compile time (handover H401).
enum class RuleConditionAt {
    /// The condition holds for the tuple, whatever the values are: the tuple is an instance of
    /// the law. A condition that reads only types (`is_float::<T>()`) answers this.
    Holds,
    /// The condition is false for the tuple: the law is not written for it, and the tuple is not
    /// an instance. A conjunction whose type-level part is false answers this without looking at
    /// the value-level part (`is_float::<T>() && combineSameSign(c1, c2)` at `T = int`).
    Fails,
    /// The condition's type-level part holds, but the rest of it reads values: the tuple is an
    /// instance whose condition depends on what it matches (`combineSameSign(c1, c2)`), and
    /// whether it fires is measured sample by sample, as it is for an ordinary conditional rule.
    ValueDependent,
    /// The condition could not be asked at all (`why` says why: the builder that declared the
    /// rule is gone, or the condition is written in a way the compiler cannot evaluate). The
    /// tuple is kept: a condition the check cannot answer must not take an instance away.
    Unknown,
};

/// What the condition of the generic rule `ruleId` says about `types`, asked at compile time,
/// where the rule was declared. A rule without a condition holds for every tuple. Nothing is
/// instantiated: this is the type-level filter `check` applies before it tries a tuple.
RuleConditionAt ruleConditionAt(
    camel::core::context::Context &context, uint64_t ruleId,
    const std::vector<camel::core::type::Type *> &types, std::string &why);

/// The types the operator groups registered (OperatorTable::allRegisteredTypes): the candidate set
/// `check` enumerates the instances of a generic rule over when it is given no program. Built when
/// it is asked for, from the declarations the loaded modules made.
std::vector<camel::core::type::Type *> ruleInstanceTypes();

/// The rules the context has registered, in the order they were registered: `ruleId - 1` indexes
/// it. Rules are registered while a library is compiled and live as long as the context, which is
/// what lets a rule value keep only an id.
std::vector<std::shared_ptr<GenericRuleEntry>> &
genericRuleTable(camel::core::context::Context &context);

/// The rule registered under `ruleId`, or null when nothing is registered under it. An ordinary
/// rule's value keeps 0, so no rule is ever registered under it.
GenericRuleEntry *genericRuleEntry(camel::core::context::Context &context, uint64_t ruleId);

/// Registers the templates of one generic rule and answers the id its value keeps: ids start at 1,
/// and 0 means "not a generic rule" everywhere. `owner` is the builder the templates belong to,
/// and `typeParams` the type parameters of the rule's head.
uint64_t registerGenericRule(
    camel::core::context::Context &context, void *owner, std::string name,
    std::vector<std::string> typeParams, const std::shared_ptr<void> &lhs,
    const std::shared_ptr<void> &rhs, const std::shared_ptr<void> &when);

/// The place, in its rule's `instances`, of the instance of `ruleId` at `types`: the tuple is
/// instantiated once and kept, and asking twice answers the same place. The attempt never reports
/// a diagnostic — read the entry's instance for whether it was made and why not — so a type the
/// rule cannot be written for costs nothing but the attempt. The entry's vector may grow during
/// the call: read it from `genericRuleEntry` again.
size_t genericRuleInstance(
    camel::core::context::Context &context, uint64_t ruleId,
    const std::vector<camel::core::type::Type *> &types);

} // namespace camel::compile::gir