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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The operators that are calls of a function (see operator_names.h).
 */

#include "camel/core/operator_names.h"

#include <array>
#include <limits>
#include <unordered_map>

namespace camel::core {

namespace {

constexpr size_t kAny = std::numeric_limits<size_t>::max();

/// One overloadable form: the symbol with a number of parameters from `minParams` to
/// `maxParams` is a call of `group`.
struct OverloadableForm {
    std::string_view symbol;
    size_t minParams;
    size_t maxParams;
    std::string_view group;
};

// The binary operators, the unary ones, and indexing (`m[i]`, `m[i, j]`: the indexed value and
// the indices). `-` is both: one parameter is the negation, two the subtraction.
constexpr std::array<OverloadableForm, 19> kForms = {{
    {"+", 2, 2, "__add__"},
    {"-", 2, 2, "__sub__"},
    {"-", 1, 1, "__neg__"},
    {"*", 2, 2, "__mul__"},
    {"/", 2, 2, "__div__"},
    {"%", 2, 2, "__mod__"},
    {"^", 2, 2, "__pow__"},
    {"@", 2, 2, "__mat__"},
    {"==", 2, 2, "__eq__"},
    {"!=", 2, 2, "__neq__"},
    {"<", 2, 2, "__lt__"},
    {"<=", 2, 2, "__le__"},
    {">", 2, 2, "__gt__"},
    {">=", 2, 2, "__ge__"},
    {"&", 2, 2, "__and__"},
    {"|", 2, 2, "__or__"},
    {"!", 1, 1, "__not__"},
    {"~", 1, 1, "__inv__"},
    {"[]", 2, kAny, "__idx__"},
}};

} // namespace

std::optional<std::string_view> overloadableOperatorGroup(std::string_view symbol, size_t paramCount) {
    for (const OverloadableForm &form : kForms) {
        if (form.symbol == symbol && paramCount >= form.minParams && paramCount <= form.maxParams) {
            return form.group;
        }
    }
    return std::nullopt;
}

std::optional<std::string_view> whyNotOverloadable(std::string_view symbol) {
    // `!=` is an operator of the program, but it follows `==` (design principles 2.13):
    // an overload of `==` gives it, so that the two cannot disagree. Its group stays in
    // kForms, where the compiler looks it up to apply that overload.
    if (symbol == "!=") {
        return "it follows an overload of '==': 'a != b' is '!(a == b)' with that overload, so "
               "the two cannot disagree";
    }
    for (const OverloadableForm &form : kForms) {
        if (form.symbol == symbol) {
            return std::nullopt;
        }
    }
    static const std::unordered_map<std::string_view, std::string_view> reasons = {
        {"&&", "it is a branch, 'a && b' is 'if a then b else false', whose right side is "
               "evaluated only when needed"},
        {"||", "it is a branch, 'a || b' is 'if a then true else b', whose right side is "
               "evaluated only when needed"},
        {"??", "it is a branch on whether the value on its left is null"},
        {"!!", "it is a branch on whether the value on its left is an error"},
        {"?->", "it is a branch on whether the value on its left is an error"},
        {"->", "it passes the value on its left to the function on its right"},
        {"..", "it composes two functions"},
        {"=", "assignment binds a variable to a value"},
        {"+=", "a compound assignment is the assignment of the plain operator: 'a += b' is "
               "'a = a + b', so it follows an overload of '+'"},
        {"-=", "a compound assignment is the assignment of the plain operator: overload '-'"},
        {"*=", "a compound assignment is the assignment of the plain operator: overload '*'"},
        {"/=", "a compound assignment is the assignment of the plain operator: overload '/'"},
        {"%=", "a compound assignment is the assignment of the plain operator: overload '%'"},
        {"^=", "a compound assignment is the assignment of the plain operator: overload '^'"},
        {"@=", "a compound assignment is the assignment of the plain operator: overload '@'"},
        {"&=", "a compound assignment is the assignment of the plain operator: overload '&'"},
        {"|=", "a compound assignment is the assignment of the plain operator: overload '|'"},
        {"===", "it is not part of the language"},
        {"!==", "it is not part of the language"},
        {".", "it reads a field"},
        {"=>", "it is not an operator"},
    };
    if (const auto it = reasons.find(symbol); it != reasons.end()) {
        return it->second;
    }
    return "it is not an operator that is a call of a function";
}

std::string_view arityOf(std::string_view symbol) {
    if (symbol == "-") {
        return "one parameter (negation) or two (subtraction)";
    }
    if (symbol == "[]") {
        return "two or more parameters (the indexed value and the indices)";
    }
    for (const OverloadableForm &form : kForms) {
        if (form.symbol == symbol) {
            return form.minParams == 1 ? "one parameter" : "two parameters";
        }
    }
    return {};
}

bool isOverloadableOperatorGroup(std::string_view group) {
    for (const OverloadableForm &form : kForms) {
        if (form.group == group) {
            return true;
        }
    }
    return false;
}

const char *operatorSymbolOf(std::string_view group) {
    static const std::unordered_map<std::string_view, const char *> symbols = {
        {"__assn__", "="},        {"__assn_add__", "+="},  {"__assn_sub__", "-="},
        {"__assn_mul__", "*="},   {"__assn_div__", "/="},  {"__assn_mod__", "%="},
        {"__assn_mat__", "@="},   {"__assn_pow__", "^="},  {"__assn_and__", "&="},
        {"__assn_or__", "|="},    {"__or__", "|"},         {"__and__", "&"},
        {"__eq__", "=="},         {"__neq__", "!="},       {"__strict_eq__", "==="},
        {"__strict_neq__", "!=="}, {"__lt__", "<"},        {"__le__", "<="},
        {"__gt__", ">"},          {"__ge__", ">="},        {"__add__", "+"},
        {"__sub__", "-"},         {"__mul__", "*"},        {"__div__", "/"},
        {"__mod__", "%"},         {"__mat__", "@"},        {"__pow__", "^"},
        {"__idx__", "[]"},        {"__not__", "!"},        {"__neg__", "-"},
        {"__inv__", "~"},
    };
    const auto it = symbols.find(group);
    return it == symbols.end() ? nullptr : it->second;
}

std::string displayNameOf(std::string_view name) {
    if (isOverloadableOperatorGroup(name)) {
        return std::string("operator ") + operatorSymbolOf(name);
    }
    return std::string(name);
}

} // namespace camel::core
