// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/**
 * @file translate.h
 * @brief The translation layer of the rule verifier (docs/design/rewrite/plan-verification.cn.md,
 * S1): a rule's two sides as logic terms, and the declaration of what it covers.
 *
 * Rewrite design 8.1 splits the field in two: what a translation covers, and what it does not. A
 * rule the layer cannot translate is refused **with a reason** -- the layer never guesses and never
 * approximates, because a verdict read off a guessed translation would be a lie (the rule on `verification-verdict-surface`, 3: a rule outside the coverage is `sampled` and the
 * reason is written on its row).
 *
 * Slice S1 translates the **integer scalar** fragment: the widths `int` and `i32`, the operators
 * `+ - * / %` and unary `-`, integer constants and the rule's own parameters. Everything else -- the
 * floating widths `f64`/`f32`/`f16`/`bf16` (S3), a whole-tensor property, a region-carrying pattern,
 * a call outside the fragment -- is refused by name. `coverageTable()` is that declaration as a
 * value, so the boundary is something a reader and a test can check rather than a paragraph.
 *
 * A rule's `/` and `%` fault when the divisor is 0 and wrap at -1 (design 1.8): the language has no
 * undefined behaviour, so the term carries the operation as it is written and the fault is a
 * property of it that the decision procedure of S2 has to carry, not something this layer may drop.
 *
 * The layer is a library of the rewrite module, not a Camel module: it registers no operator and
 * has no `.cml` declaration, so it is not a `.cmo` (modules/verify/CMakeLists.txt).
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/rules.h"

#include <cstdint>
#include <memory>
#include <string>

namespace camel::verify {

/// The integer widths the fragment covers. The floating widths the design lists beside them
/// (`f64`, `f32`, `f16`, `bf16`) are declared in the coverage table and not translated yet.
enum class Width : uint8_t {
    Int32,
    Int64,
};

/// The name the report writes for a width: the Camel type name `i32` or `int`.
const char *widthName(Width width);

struct Term;
/// Terms are shared, not owned by one parent: a parameter used twice is one term.
using TermPtr = std::shared_ptr<const Term>;

/// One node of an integer scalar term: the logic layer a solver reads.
struct Term {
    enum class Kind : uint8_t {
        Constant,  ///< an integer literal
        Parameter, ///< one of the rule's parameters, named as the rule declares it
        Add,
        Sub,
        Multiply,
        Divide,
        Remainder,
        Negate,
    };

    Kind kind    = Kind::Constant;
    Width width  = Width::Int64;
    /// `Constant`: the value as its two's complement bits, `width` bits wide.
    uint64_t value = 0;
    /// `Parameter`: its position among the rule's own parameters.
    uint32_t parameter = 0;
    /// `Parameter`: the name the declaration gives it, empty when the signature records none.
    std::string name;
    /// `Add`, `Sub`, `Multiply`, `Divide` and `Remainder`: the left operand; `Negate`: the operand.
    TermPtr left;
    /// The binary operators' right operand.
    TermPtr right;
};

/// The term as the report writes it: every binary operator parenthesized, a constant as the signed
/// decimal value of its width, a parameter by the name the rule declares. `(x + 0)`, `(-x)`, `7`.
std::string render(const Term &term);

/// What one attempt to translate a rule's two sides produced.
struct Translation {
    /// True when both sides are terms of the integer scalar fragment.
    bool covered = false;
    /// `covered`: the two sides, as the proposition `lhs = rhs`.
    TermPtr lhs;
    TermPtr rhs;
    /// `covered`: the width the two sides work in.
    Width width = Width::Int64;
    /// `covered`: whether the declaration carries a condition. The sides are translated, and the
    /// condition is not a premise of the proposition yet (S4); the column says so.
    bool conditional = false;
    /// Not `covered`: which of the declared reasons refused it.
    std::string reason;

    /// The proposition the translation made: `(x + 0) = x`.
    std::string proposition() const;
    /// The coverage column of the report row: `integer scalar int: (x + 0) = x, no premise yet`, or
    /// `beyond translation coverage: <reason>`.
    std::string column() const;
};

/// Translate the two sides of `source`. Never throws and never guesses: a rule outside the fragment
/// comes back with `covered == false` and the reason.
Translation translate(const camel::runtime::RuleSource &source);

/// The coverage column of a rule whose two sides were never built (the check could not make an
/// instance, or has no template for it): the reason is the caller's, and the column says the
/// translation was not attempted rather than that it failed.
std::string uncoveredColumn(const std::string &reason);

/// The declared coverage of the layer: which widths and operators it translates, and what it
/// refuses. This is the capability table of design 8.1 as a value, and the report publishes it.
const std::string &coverageTable();

} // namespace camel::verify