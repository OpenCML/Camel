// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/compile/gir/rule_templates.h"
#include "camel/parse/ast/rule_head.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace camel::transforms {

/// The position that asks for the whole answer: a right side that answers one value, taken as it
/// stands. Every other position asks for one name's cotangent.
inline constexpr size_t kWholeAnswer = static_cast<size_t>(-1);

/// The ports a rule instance asks for, in port order -- `T[..]|T`'s two, and so on. This is what a
/// choice between two laws of one key is made on.
std::vector<camel::core::type::Type *>
rulePortsOf(const camel::compile::gir::GenericRuleInstance &instance);


/// The ports of the closure a law's right side answers with: the **tangents** it takes, in the order
/// its right side writes them, or empty when the right side is not a function of the tangents.
///
/// This is not the same question `rulePortsOf` answers. That one is the *left* side -- the operator's
/// own operands -- and it is what a law is chosen on (one law per left side; `chooseRuleAmong`). What
/// a law will *read* when the walk hands it a cotangent is written nowhere on the left side: it is
/// the parameter of the closure the right side answers with (`(ct: T[..]) => mean_axis_cotangent(ct,
/// at)`), which `findRuleRightSide` already reads in order to expand that closure. A family that must
/// judge a cotangent against the port it is entering asks here.
std::vector<camel::core::type::Type *>
ruleCotangentPortsOf(const camel::compile::gir::GenericRuleInstance &instance);

/// The extents a type's shape states, or nothing when the type leaves one of them open.
///
/// A shape is a fact about an array type: `float[2, 3]` states two extents, `float[]` states one
/// dimension and leaves its length open, and `float[..]` states neither the rank nor a length. The
/// reading of a stated extent is the one `jvp.cpp#zeroConstantFor` makes of the same fact --
/// `ExtentForm::evaluate({})` answers the number a written extent holds and nothing for a `?` or for
/// an extent over a name nothing binds -- so the transforms agree about which types have a shape that
/// can be written while the graph is built.
///
/// There is one definition of this reading because there is one answer to give: `transpose` builds a
/// shape carrier out of it (`shapeCarrierOf`) and `expandRuleRightSide` hands the same numbers to a
/// law that asks for an operand's shape
/// (`mul_tensor_t`'s `shape(x)`, `stdlib/rulesets/transpose.cml`). Two copies of it would be two
/// answers the day one of them learned about a type form the other did not.
std::optional<std::vector<int64_t>> statedShapeOf(const camel::core::type::Type *type);

/// Whether `narrow` asks for a type every position `wide` also takes, and asks for a narrower one
/// somewhere: `T[]` beside `T[..]`, where an open rank takes a rank-one array and a rank-one array
/// does not take an open rank. This is the relation a choice between two laws of one key is made by,
/// and it is `assignableFrom` in the direction a call checks -- the port takes the operand -- not
/// equality: `T[..]` and `f32[4]` are not the same type, and the law written `T[..]` is the law for a
/// `f32[4]` operand.
bool narrowerPortsThan(
    const std::vector<camel::core::type::Type *> &narrow,
    const std::vector<camel::core::type::Type *> &wide);

/// Which of a key's laws answers a node, and `why` when the declaration does not choose.
///
/// A key holds one law in most families, and then this answers that law. It holds several when one
/// spelling names several shapes -- `t[0]` on a `T[]` is the element and on a `T[..]` it is the
/// open-rank array (design principles 5.4), and `float(x)` from a float carries a tangent while from
/// an `int` it does not (`stdlib/rulesets/jvp.cml`) -- and then the operands decide: an instance's
/// ports have to take them (`Type::assignableFrom`, the direction a call checks, never equality), and
/// among the laws that take them the narrowest wins (`T[]` beside `T[..]`, where an open rank takes a
/// rank-one array and a rank-one array does not take an open rank).
///
/// Two laws that both take the operands and neither of which is narrower than the other are a
/// declaration the transform will not choose between: picking by filing order would make a program
/// mean one thing where a rule sits and another where it is moved to, so the refusal names both laws
/// and the types they were offered.
///
/// The three families that ask this -- `jvp`, `transpose` and `vmap` -- ask it in this one place
/// (`chooseRuleAmong`); what differs between them is the words a refusal is written with and the type
/// tuple an instance is made at, and both are arguments.
/// The three ways a key answers, and the difference between the last two is a reader's: which layer
/// of the declaration refused. Both are "nothing to expand here", and they are not the same sentence.
enum class RuleRefusal {
    /// A law answered.
    None,
    /// No law of this key describes this node: the index filed none, or the instance a candidate
    /// named was not made, or a condition the law writes does not hold here.
    NoLaw,
    /// A law of this key **was** found and instantiated, and its own parameter list does not take the
    /// operands -- the step is not the shape the law was written for. The key has a law, so the
    /// responsibility is the law's, not the next step's; `transpose` has a word for this
    /// (`skip=rule-shape`) and a sentence that names the law's two type lists, and the case that
    /// exists to keep the two apart (`showcase_s2`, `matmul_t.vector_operand_is_refused_by_shape`)
    /// fails the day they are merged into one.
    Shape,
};

struct RuleChoice {
    /// The law that answers, null when none does.
    const camel::compile::gir::GenericRuleInstance *instance = nullptr;
    /// The law's name, as the index and the log write it.
    std::string rule;
    /// Why nothing answered: the most specific refusal a candidate gave.
    std::string why;
    /// Which of the three answers this is. A caller that has a word of its own for a shape refusal
    /// reads this instead of parsing `why`.
    RuleRefusal refusal = RuleRefusal::NoLaw;
    /// What the two lists were, when `refusal == RuleRefusal::Shape`: the law's parameters as its
    /// left side writes them, and the operands it was asked about. The finding is this function's;
    /// the wording is the family's (`RuleChoiceWords`), so the sentence is built where the words are.
    std::vector<camel::core::type::Type *> lawPorts;
    std::vector<camel::core::type::Type *> operandTypes;
    bool found() const { return instance != nullptr; }
};

/// The words one family writes its refusals with. `jvp` has always said "the rule takes (..), and the
/// operator has (..)" and "two laws both accept these arguments"; `transpose` and `vmap` say "law" and
/// "operands". They are the same question, and a transform's log is read by people who have learned
/// its words: the words are arguments so that unifying the question does not rewrite the readings.
struct RuleChoiceWords {
    const char *subject  = "law";     // "rule" (jvp) or "law" (transpose, vmap)
    const char *operands = "operands"; // "arguments" (jvp) or "operands" (transpose, vmap)
};

/// Which of `candidates` answers a node whose ports are `operandTypes`, with an instance made at
/// `instanceTypes`.
///
/// `dataOperandCount` is how many of `operandTypes` are the operator's *data* operands: the rest are
/// its `<>` configuration (`reshape`'s target shape), which a law may name or not, and a law that
/// names fewer ports than the node has is not thereby refusing anything. A law that names fewer ports
/// than the node's *data* operands cannot describe the node, and one that names more ports than the
/// node has cannot be filled. Passing 0 asks for no lower bound at all (`vmap`, whose operand list
/// carries the configuration and whose laws are written against the value only).
///
/// `answered` says which of the ports the walk will actually hand the law's right side, when the
/// family knows: a position the walk answers no cotangent for -- a constant, a value with no tangent
/// space -- is not a position the law is asked about, so its shape cannot make the law wrong
/// (`x * 2.0` is one array operand and one constant, and the law for two arrays answers it). Null
/// asks about every port, which is what a family that has not asked itself the question does.
RuleChoice chooseRuleAmong(
    camel::core::context::Context &context,
    const std::vector<camel::parse::ast::RuleMatch> &candidates,
    const std::vector<camel::core::type::Type *> &instanceTypes,
    const std::vector<camel::core::type::Type *> &operandTypes, size_t dataOperandCount,
    const RuleChoiceWords &words = {}, const std::vector<bool> *answered = nullptr);

/// The rule's right side expanded at the match site: the body of the closure it answers with is
/// copied into this graph, its captures bound to the operands and its parameters to the tangents,
/// and the copy's exit is the operator's derivative.
///
/// Module-internal on purpose: the right side is a graph of the rule's own program, and the one
/// thing a transform needs from it is "stand this in where the operator stood". It is not a
/// general inliner and is not offered as one -- what it refuses, it refuses by name in `why`.
///
/// Two transforms reach a rule's right side without running the rule themselves: `jvp` (the
/// operator's tangent) and `transpose` (its cotangent). Neither may call the closure the right
/// side answers with -- a compile-time graph has no closure to call -- so the one lambda layer is
/// expanded here instead.
///
/// A rule's left side names N values and its right side answers N cotangents -- a tuple, in the
/// order the left side writes them -- so `position` picks the i-th component, and only the nodes
/// that component needs are copied: a position never asks for a value its own answer does not read.
/// `kWholeAnswer` takes the exit itself, which is what a right side that answers one value means.
camel::runtime::gc_node_ref_t expandRuleRightSide(
    camel::runtime::GraphDraftGroup &group, camel::runtime::GraphTransaction &transaction,
    const camel::compile::gir::GenericRuleInstance &instance,
    const std::function<camel::runtime::gc_node_ref_t(size_t)> &valueOf,
    const std::vector<camel::runtime::gc_node_ref_t> &tangents, std::string &why,
    size_t position = kWholeAnswer);

/// The same expansion, reached from the rule's *name*: the transform that has a rule's name and its
/// operands -- and no instance, because it never runs a rule itself -- asks here.
///
/// `valueOf` answers an operand's value by the place the right side binds it, and is asked only for
/// the places the answer this `position` selects reads. That is what lets a position be answered
/// where one of the operands has no value: the component that answers it names only the other one,
/// so the walk is never asked for a value it does not carry.
///
/// Answers the expanded node, or `kInvalidNodeRef` with `why`: no rule of that name in this context,
/// an instance that did not come about, a position the right side does not answer, or a value the
/// answer reads and the walk lacks.
///
/// `noInstanceAtOperands`, when given, is set to true for the second of those four and false
/// otherwise: the law was named, and it would not instantiate at `operandTypes`. That is the one of
/// the four a caller acts on differently -- a law that dispatches on the operands' shape (its
/// cotangents written once per rank) refuses exactly the shapes it has no overload for, and a
/// transform that must name the refusal needs to say which class of refusal it met rather than read
/// `why`, whose sentences belong to the instantiation. `why` still says what the instantiation
/// complained about.
camel::runtime::gc_node_ref_t expandRuleByName(
    camel::core::context::Context &context, camel::runtime::GraphDraftGroup &group,
    camel::runtime::GraphTransaction &transaction, std::string_view ruleName,
    const std::function<camel::runtime::gc_node_ref_t(size_t)> &valueOf,
    const std::vector<camel::runtime::gc_node_ref_t> &tangents, size_t position,
    const std::vector<camel::core::type::Type *> &operandTypes, std::string &why,
    bool *noInstanceAtOperands = nullptr);

} // namespace camel::transforms