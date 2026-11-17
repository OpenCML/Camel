// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The operators of the language that are calls of a function, and the reserved names of
 * those functions.
 *
 * `a + b` is the call `__add__(a, b)`: the operator group `__add__` holds the built-in
 * overloads and those of the C++ modules, and a program adds its own with
 * `func operator +(a: V, b: V): V` (design principles 4.3; the notation is provisional).
 * This table is the one place that says which symbol is which function, which symbols can be
 * overloaded and with how many parameters, and why the others cannot: the parser takes any
 * operator token after `func operator`, the compiler checks the declaration here, the
 * resolution of a call recognises an operator by its group name, and the diagnostics and the
 * language server show the symbol instead of the reserved name.
 *
* Not in the table as overloadable: `&&` and `||` are branches (4.2); `??` and `!!` are
 * branches on the mark of a `T?` / `T!`; `->` passes a value to a function (and carries a null
 * or an error along); `..` composes two
 * functions; `=` and the compound assignments write a variable (`a += b` is `a = a + b`, so it
 * follows `+`); `===` and `!==` are not part of the language.
 */

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace camel::core {

/// The function of the operator `symbol` declared with `paramCount` parameters: `__add__` for
/// `+` with two, `__neg__` for `-` with one, `__idx__` for `[]` with two or more. nullopt when
/// that operator cannot be overloaded with that many parameters (whyNotOverloadable and
/// arityOf say why).
std::optional<std::string_view>
overloadableOperatorGroup(std::string_view symbol, size_t paramCount);

/// The group a binary operator written `symbol` calls: `__add__` for `+`, `__assn_add__` for
/// `+=`, `__assn__` for `=`, `__idx__` for `[]`. One table for the plain operators, the compound
/// assignments and the groups the compiler looks them up by; nullopt for a symbol that is not
/// a call of a function (`&&`, `||`, `===`, ...).
std::optional<std::string_view> binaryOperatorGroup(std::string_view symbol);

/// The name the compiler writes for `a..b` before the two readings of the construct are told
/// apart (design principles 4.2; the rule on
/// the rule): between two functions it is the composition `f..g`, the
/// lambda `(x) => g(f(x))`, and between two integers it is the interval `[a, b)`. The graph
/// builder writes the construct as one call of this name and the GIR builder resolves it, since
/// only there are the types of the two operands known. No program writes the name.
inline constexpr std::string_view kCompositionCall = "$comp::..";

/// The operator of `fail<'Kind'>(message)` (design principles 3.9), declared in
/// `stdlib/prelude.cml` and built in as the group `fail`: the program's own premise
/// check, whose call gives the bottom type `never`. A program never writes the URI; the builder
/// reads the nodes of the calls back by it, for what a `fail` written as a statement of its own
/// means (it is no effect, so nothing runs it where its value is not wanted) and for the
/// statements it leaves unreachable.
inline constexpr std::string_view kFailOperatorUri = ":op/fail";

/// The plain operator a compound assignment follows: `__add__` for `__assn_add__`; nullopt
/// for any other group.
std::optional<std::string_view> plainGroupOfCompoundAssignment(std::string_view group);

/// Why the operator `symbol` cannot be overloaded at all (for the diagnostic), or nullopt when
/// it can be overloaded for some number of parameters.
std::optional<std::string_view> whyNotOverloadable(std::string_view symbol);

/// How many parameters an overload of `symbol` takes, in words ("two parameters", ...), for the
/// diagnostic of a declaration with another number. Empty when the symbol cannot be overloaded.
std::string_view arityOf(std::string_view symbol);

/// True when `group` is the reserved name of an operator a program can overload
/// (`__add__`, `__neg__`, `__idx__`, ...).
bool isOverloadableOperatorGroup(std::string_view group);

/// How a program writes the operator behind the group `group` (`+` for `__add__`, `+=` for
/// `__assn_add__`, `[]` for `__idx__`); nullptr for a group that is not an operator of the
/// language.
const char *operatorSymbolOf(std::string_view group);

/// The name to show for a function called `name`: `operator +` for `__add__`, `name` itself
/// otherwise. Diagnostics and the language server use it; the reserved name is never shown
/// for an overload a program declared.
std::string displayNameOf(std::string_view name);

} // namespace camel::core
