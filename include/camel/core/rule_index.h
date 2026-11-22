// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The rules a ruleset module files, as the kernel keeps them (core abstractions §6, §7).
 *
 * A ruleset is a compile-time value. The compiler reads a `rule` declaration once -- when the
 * module's declarations are read, where the rule's two sides are still syntax -- and what it learns
 * about it is filed here: the rule's name, the operator its left side is written with, how many
 * operands that operator takes, the function its right side calls, and the names its left side
 * binds. Those five are the whole of what a transform asks about a rule, and they are answered from
 * a member of `RuleObject` ever after: nothing downstream reads the syntax tree again, and nothing
 * downstream re-derives them from the rule's name.
 *
 * This is not the rewrite module's rule *value* (`modules/rewrite/rule.h`), which is the runtime
 * object a program's `rule` expression builds and an engine matches with; this is what the compiler
 * filed about the declaration, and a transform reads it before any value exists.
 *
 * The split this file exists for: the *reader* of a declaration is the parser (its rule-head
 * reader takes the head off the left side, the callee off the right side, and the names off both),
 * and the *store* is the kernel. The parser writes into this index and never asks it a question; a
 * transform asks it questions and never sees a syntax node. That is the direction the layering
 * needs: nothing under `include/camel/core/` or `modules/` includes a syntax-tree header.
 *
 * A family is a ruleset module (`rulesets.jvp`, `rulesets.transpose`, `rulesets.vmap`), and it is
 * keyed by the head of a rule's left side and that head's operand count: `-x` and `x - w` are two
 * primitives (`__neg__` and `__sub__`) written with one token, so a key without the count files two
 * rules as one. A key holds one **or more** rules, because one spelling can name several operators
 * (`float(x: float)` and `float(x: int)` are both `float` at one operand) and the *type* is what
 * tells their laws apart -- which is the engine's question, asked where the operand's type is known
 * (the engine instantiates a rule at the operand's own type and refuses by name when no instance is
 * made).
 *
 * One member is not read off the declaration at all: `parameterTypes` is
 * what the declaration *compiled to*, and they are filed when the module is built, from the
 * signatures the compiler made of the rule's own sides. That is deliberate -- a rule's parameter
 * types are types, and the text a declaration was written with is not a type: two spellings of one
 * type would be two laws here, and two types that spell the same would be one. The compiled form is
 * the only truth about them, so the text is not kept beside it.
 */

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace camel::core::type {
class Type;
class FunctionType;
} // namespace camel::core::type

namespace camel::core {

/// One rule as the compiler filed it. The first five members are the rule head's answer to every
/// question a transform asks about a rule; the rest is what the compiler needs to judge the
/// declaration itself (a duplicate, a left side that is not an operator, a diagnostic's position)
/// and what the readings of the index print.
struct RuleObject {
    /// The rule's name (`jvp_add`, `vmap_mul`): what the engine's rule value is called.
    std::string name;
    /// The operator the rule's left side is written with: the token of `a <op> b` and `<op> a`, or
    /// the callee of `f(a, b)`. Empty when the left side is none of those, which `shape` says.
    std::string head;
    /// How many operands that head takes: 2 for `a + b`, 1 for `-a`, the argument count for a call.
    uint32_t arity = 0;
    /// The function the rule's right side calls (`add_tangent`, `neg_tangent`, ...), empty when the
    /// right side calls nothing (it is a value). The name a replacement has to resolve.
    std::string rhsCallee;
    /// The names the left side binds, in the order it writes them (`a / b` gives `a,b`), joined by
    /// commas: the tuple both sides of the rule take.
    std::string variables;

    /// When the left side is not one operator: the shape's name, else empty. Read where the rule is
    /// written, reported there, and never asked again.
    std::string shape;
    /// Which of `variables` the right side *also* writes (`x` for `(bx, bw) => vmap_add(bx, x)`),
    /// joined by commas; empty when it reads none of them. A family whose transform calls only the
    /// right side's callee has nothing to bind those names with, so a rule that reads one is
    /// refused where it is written.
    std::string outerUses;
    /// There is no `condition` member here, though the compiler does compile one. A rule's
    /// applicability condition belongs to the rule value the transform carries, and the match is
    /// what reads it; this type had a field for it that nothing ever read, so the field is gone
    /// and the signature stays where the transform step will pick it up
    /// (`src/compile/gir/program.cpp`, `compiledRuleTypes`). Keeping a written-but-unread field
    /// would be a claim that something asks, which nothing does.
    /// The type of each of the rule's parameters, in order, as the compiler compiled it: the
    /// parameter types of the left side's signature (`<rule>__lhs`), which are the types the rule
    /// matches with. Empty for a rule that takes none, and nullopt when the left side was never
    /// compiled into a signature at all (a ruleset module's rule whose maker the program did not
    /// settle) -- the two are different answers, and `indexRule` gives a verdict only on the first.
    ///
    /// This is the other half of a duplicate: two laws of one key whose parameter types are the
    /// same list can answer for the same operands and cannot be told apart, while two whose lists
    /// differ are two laws of two shapes (design principles 5.4).
    std::optional<std::vector<const camel::core::type::Type *>> parameterTypes;
    /// The rule's tokens in its module's buffer, for a diagnostic about the rule itself.
    uint32_t tokenStart = 0;
    uint32_t tokenEnd   = 0;
};

/// Why a lookup of a rule answered the way it did.
enum class RuleLookup {
    Found,              // the family has a rule for that operator with that many operands
    NoSuchFamily,       // no ruleset module of that name was built into this program
    NoRuleForPrimitive, // the family is there and holds no rule for that operator and count
};

/// What a lookup answers: the rule when there is one, and why there is not otherwise. The rule is
/// the object the compiler filed -- not its name, which the caller would have to ask about again.
struct RuleMatch {
    RuleLookup status = RuleLookup::NoSuchFamily;
    RuleObject rule; // filled when `status` is Found; default-constructed otherwise
};

// --- the writer's side: the compiler files what it read off a declaration ---------------------

/// Records one rule of a ruleset while its declarations are read. The module is named by the
/// caller: the index is keyed by module, never by an ambient "current module". `rule` (the rule's
/// name), `head`, `arity`, `rhsCallee` and `variables` are the five facts a transform will read
/// back; `shape` and `outerUses` are what the module build judges the declaration by. The other
/// one -- `parameterTypes` -- is not here: it is a type, a declaration read as
/// syntax has none, and it is filed from the compiled sides when the module is built
/// (`camel/compile/gir/rule_templates.h#compiledRuleTypes`).
void collectRuleHead(
    const std::string &module, std::string head, std::string rule, std::string rhsCallee,
    std::string shape, std::string variables, std::string outerUses, uint32_t arity,
    uint32_t tokenStart, uint32_t tokenEnd);

/// The rules collected for `module`, removed by the read: one registry per ruleset module, taken
/// once where the module is built.
std::vector<RuleObject> takeRuleHeads(const std::string &module);

/// Files `rule` in `family`'s index under the head of its left side and that head's operand count.
/// The name of a *different* rule already filed under that key is returned -- the caller reports
/// the duplicate, and the two are never silently merged -- and an empty string means the rule was
/// filed, or was already there under the same name (a module built twice files its rules twice).
///
/// Two rules under one key are a duplicate when they answer for the same operands: their parameter
/// types, as the compiler compiled them, are one list (`type::typeEquals`, the type system's own
/// answer -- not the text the two declarations were written with). Two whose lists differ are two
/// laws of two shapes, and the choice between them is made where the operand types are known (the
/// engine's, not this index's). A rule whose left side was never compiled has no list to compare,
/// and no verdict is a refusal: a duplicate is reported only where it can be seen to be one.
std::string indexRule(const std::string &family, const RuleObject &rule);

// --- the reader's side: a transform asks the index, and is handed the objects ------------------

/// The rule of `family` whose left side is `head` with `arity` operands. A ruleset that is only
/// built and printed answers nothing, and reads as if it did: this is the question a transform asks
/// it, and the answer says which of the two ways there is no rule.
RuleMatch lookupRule(const std::string &family, const std::string &head, uint32_t arity);

/// Every rule of `family` whose left side is `head` with `arity` operands, in the order they were
/// filed. A key holds **one or more** rules, and which of them answers is decided by the engine,
/// which instantiates a rule at the operand's own type and refuses by name when there is none. The
/// index files the name and the compiled types; the choice among them is the engine's.
std::vector<RuleObject>
lookupRules(const std::string &family, const std::string &head, uint32_t arity);

/// How many operands the rule whose left side is `head` takes, when the family holds exactly one
/// such rule; nullopt when it holds none, or more than one (`-` at one operand and at two are two
/// rules, and a caller that has only the spelling must not guess which). This is the question an
/// operator whose rule is written as a call asks about its own name: `tensor:sum_axis` is
/// `sum_axis` at whatever count the family filed it under, and the count is not in the name.
std::optional<uint32_t> arityOfHead(const std::string &family, const std::string &head);

/// The function `rule` of `family` calls on its right side, or empty when its right side calls
/// nothing: the same answer `lookupRule(family, ...).rule.rhsCallee` gives, asked by name.
std::string rhsCalleeOf(const std::string &family, const std::string &rule);

/// The families built so far, in name order: what a reader can ask `lookupRule` about.
std::vector<std::string> ruleFamilies();

/// Everything `family` holds, in key order (`head` then `arity`): what a printer of the index
/// reads, and the whole family for a reader that wants every rule of it (the operator table's
/// re-interpretation reading walks the forward family this way).
std::vector<RuleObject> rulesOf(const std::string &family);

} // namespace camel::core
