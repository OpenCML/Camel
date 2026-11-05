// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/forward.h"
#include "camel/core/operator_table.h"
#include "camel/core/type/base.h"

#include <array>

namespace camel::core {

namespace {

/// The operations of the language that are written as a token, and what each is written with. The
/// table holds the ones whose **result has a tangent space**, because those are the ones a transform
/// has to ask about: `%`, `==`, `<`, `&&` and `!` are written tokens too, and they are absent because
/// their results are integers and booleans -- no space, no question. `^` was the one name missing
/// from that list with a float result (`pow_d` is a float raised to a float), which made `x ^ 2.0`
/// impossible to differentiate at all; it is here now.
///
/// The name is the operator's own (`:op/add_d` names `add`), so this table is about the *language's
/// spelling*, not about which operators exist: a module that declares another `add` overload is
/// spelled `+` too, and nothing here has to be told about it.
struct WrittenOperation {
    const char *name; // the name inside the URI, before its type suffix: `add` in `:op/add_d`
    const char *head; // the token the source writes it with: `+`
    uint32_t arity;
};

constexpr std::array<WrittenOperation, 6> kWrittenOperations{{
    {"add", "+", 2},
    {"sub", "-", 2},
    {"mul", "*", 2},
    {"div", "/", 2},
    {"neg", "-", 1},
    {"pow", "^", 2},
}};

/// The operators whose key cannot be derived, and why. They are written up in
/// `docs/handover/reviews/questions-2026-11-01-how-does-an-operator-without-a-token-get-a-key.md`;
/// the reason is repeated here because this is the code a reader of the key finds first.
///
/// The six axis reductions are the same question in one shape: their URI keeps an identity of the
/// implementation (`tensor:mean_axis`), while the one spelling a program writes is the whole
/// reduction's name with the axis and the flag in angle brackets -- `mean<axis, keepdims>(xs)` --
/// so their laws are filed under `mean` at one operand, and the name inside the URI says neither
/// (author's ruling, 2026-11-05: a reduction over an axis has one name).
const std::array<RuleKeyException, 8> kRuleKeyExceptions{{
    {":op/idx_arr",
     "index_into",
     2,
     "`xs[i]` is a bracket form, which is none of `a <op> b`, `<op> a` or `f(a, b)`, so the "
     "operator has no token a rule's left side could be written with; its rule is filed under the "
     "call `index_into(x, i)`, whose body the operator is"},

    {"tensor:argmax_axis",
     "argmax",
     1,
     "the axis reduction is the whole form's name with the axis in `<>` (`argmax<axis, keepdims>("
     "xs)`), so its rule is filed under `argmax` at the one operand the parentheses carry"},

    {"tensor:argmin_axis",
     "argmin",
     1,
     "the axis reduction is the whole form's name with the axis in `<>` (`argmin<axis, keepdims>("
     "xs)`), so its rule is filed under `argmin` at the one operand the parentheses carry"},

    {"tensor:max_axis",
     "max",
     1,
     "the axis reduction is the whole form's name with the axis in `<>` (`max<axis, keepdims>(xs)`)"
     ", so its rule is filed under `max` at the one operand the parentheses carry"},

    {"tensor:mean_axis",
     "mean",
     1,
     "the axis reduction is the whole form's name with the axis in `<>` (`mean<axis, keepdims>("
     "xs)`), so its rule is filed under `mean` at the one operand the parentheses carry"},

    {"tensor:min_axis",
     "min",
     1,
     "the axis reduction is the whole form's name with the axis in `<>` (`min<axis, keepdims>(xs)`)"
     ", so its rule is filed under `min` at the one operand the parentheses carry"},

    {"tensor:multiply",
     "mul_tensor",
     2,
     "`*/2` is already `:op/mul_d`'s key and one key holds one rule, so the array product's rule "
     "is filed under the call `mul_tensor(x, w)`"},

    {"tensor:sum_axis",
     "sum",
     1,
     "the axis reduction is the whole form's name with the axis in `<>` (`sum<axis, keepdims>(xs)`)"
     ", so its rule is filed under `sum` at the one operand the parentheses carry"},
}};

/// The name inside an operator URI: `:op/add_d` gives `add_d`, `tensor:sum_axis` gives `sum_axis`.
std::string nameOf(const std::string &uri) {
    const size_t colon = uri.rfind(':');
    return colon == std::string::npos ? uri : uri.substr(colon + 1);
}

/// The operation a `:op/` name spells: `add_d` gives `add`, `neg_f` gives `neg`. The type suffix is
/// whatever follows the first `_`; an operation written without one (`:op/add`) is its own name.
std::string operationOf(const std::string &name) {
    const size_t underscore = name.find('_');
    return underscore == std::string::npos ? name : name.substr(0, underscore);
}

/// The key of a conversion: the destination's spelling at one operand, and nothing else.
///
/// Which operators are conversions, and which types each one converts between, is written where
/// the operator is registered (`builtin/executors/builtin.cpp#kConversions`, handed to
/// `OperatorTable::declareConversion`): the registration that gives the operator its
/// implementation also gives its two types, because they are what the operator *is*. This used to
/// be read off the letters of the name instead (`d` is float, `f` is f32, ...), which called
/// every name containing `to` a conversion, and `:op/var/get` -- an operator whose result is a
/// float and which converts nothing -- then got a conversion's key.
///
/// The *source* is deliberately not in the key. One spelling names several operators -- `float` at
/// one operand is `float(x: float)`, `float(x: int)`, `float(x: string)` and the rest -- so a key
/// that carried the source would be the index encoding a type, which is work the engine already
/// does: it instantiates a rule at the operand's own type and refuses by name when no instance is
/// made. The family files one rule per operand type under the spelling and `lookupRules` answers
/// them all; which of them answers is not the index's question.
std::optional<RuleKey> conversionKeyOf(const std::string &uri) {
    const auto conversion = camel::core::OperatorTable::instance().conversionTypesOf(uri);
    if (!conversion.has_value()) {
        return std::nullopt;
    }
    return RuleKey{camel::core::type::typeCodeToString(conversion->target), 1};
}

std::optional<camel::parse::ast::RuleMatch>
ruleIn(const char *family, const std::optional<RuleKey> &key) {
    if (!key.has_value()) {
        return std::nullopt;
    }
    return camel::parse::ast::lookupRule(family, key->head, key->arity);
}

} // namespace

std::span<const RuleKeyException> ruleKeyExceptions() { return kRuleKeyExceptions; }

std::vector<std::string> ruleKeyExceptionLines() {
    std::vector<std::string> lines;
    lines.reserve(kRuleKeyExceptions.size());
    for (const RuleKeyException &exception : kRuleKeyExceptions) {
        lines.push_back(
            exception.uri + " -> " + exception.head + "/" + std::to_string(exception.arity) +
            " because " + exception.why);
    }
    return lines;
}

std::optional<RuleKey> ruleKeyOf(const std::string &family, const std::string &uri) {
    for (const RuleKeyException &exception : kRuleKeyExceptions) {
        if (exception.uri == uri) {
            return RuleKey{exception.head, exception.arity};
        }
    }
    // An operator of the language itself: `:op/add_d` is written `+` with two operands, and the
    // name says which operation it is.
    constexpr std::string_view kLanguageOperator = ":op/";
    if (uri.rfind(kLanguageOperator, 0) == 0) {
        const std::string operation = operationOf(uri.substr(kLanguageOperator.size()));
        for (const WrittenOperation &written : kWrittenOperations) {
            if (operation == written.name) {
                return RuleKey{written.head, written.arity};
            }
        }
        // A conversion: `itod` is `float(x: i32)` and `dtod` is `float(x: float)` -- one spelling
        // at one operand, seven sources. The key is the destination's spelling; the source is the
        // engine's business, not the index's. The registration says which operators are conversions
        // and between which types; the URI is passed whole because that is how it is registered.
        if (const auto conversion = conversionKeyOf(uri)) {
            return conversion;
        }
        // Another `:op/` name -- a call of a function (`:op/fail`), a variable read
        // (`:op/var/get`), an operator of the core no ruleset writes a rule for. No key, no guess.
        return std::nullopt;
    }
    // A module's operator: its rule's left side is written as a call of its own name
    // (`matmul(x, w)`, `sum<axis, keepdims>(xs)`), and how many operands that takes is what the
    // family's index holds for that name.
    const std::string name = nameOf(uri);
    const auto arity       = camel::parse::ast::arityOfHead(family, name);
    if (!arity.has_value()) {
        return std::nullopt;
    }
    return RuleKey{name, *arity};
}

namespace {

/// Every rule `family` files under `uri`'s key, in filing order: the single answer a one-law key
/// holds, and every candidate a key that holds several laws holds. A family whose key says nothing
/// about the operator answers nothing, which is not the same as a family that holds no rule there.
std::vector<camel::parse::ast::RuleMatch> rulesIn(const std::string &family, const std::string &uri) {
    std::vector<camel::parse::ast::RuleMatch> answers;
    const auto key = ruleKeyOf(family, uri);
    if (!key.has_value()) {
        return answers;
    }
    const std::vector<std::string> names =
        camel::parse::ast::lookupRules(family, key->head, key->arity);
    answers.reserve(names.size());
    for (const std::string &name : names) {
        answers.push_back(camel::parse::ast::RuleMatch{camel::parse::ast::RuleLookup::Found, name});
    }
    return answers;
}

} // namespace

std::optional<camel::parse::ast::RuleMatch> forwardRuleOf(const std::string &uri) {
    return ruleIn("rulesets.jvp", ruleKeyOf("rulesets.jvp", uri));
}

std::vector<camel::parse::ast::RuleMatch> forwardRulesOf(const std::string &uri) {
    return rulesIn("rulesets.jvp", uri);
}

std::optional<camel::parse::ast::RuleMatch> transposeRuleOf(const std::string &uri) {
    return ruleIn("rulesets.transpose", ruleKeyOf("rulesets.transpose", uri));
}

std::vector<camel::parse::ast::RuleMatch> transposeRulesOf(const std::string &uri) {
    return rulesIn("rulesets.transpose", uri);
}

std::optional<camel::parse::ast::RuleMatch> vmapRuleOf(const std::string &uri) {
    return ruleIn("rulesets.vmap", ruleKeyOf("rulesets.vmap", uri));
}

std::vector<camel::parse::ast::RuleMatch> vmapRulesOf(const std::string &uri) {
    return rulesIn("rulesets.vmap", uri);
}

} // namespace camel::core