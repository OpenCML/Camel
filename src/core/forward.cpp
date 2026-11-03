// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/forward.h"

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

/// The two operators whose key cannot be derived, and why. They are written up in
/// `docs/handover/reviews/questions-2026-11-01-how-does-an-operator-without-a-token-get-a-key.md`;
/// the reason is repeated here because this is the code a reader of the key finds first.
const std::array<RuleKeyException, 2> kRuleKeyExceptions{{
    {":op/idx_arr",
     "index_into",
     2,
     "`xs[i]` is a bracket form, which is none of `a <op> b`, `<op> a` or `f(a, b)`, so the "
     "operator has no token a rule's left side could be written with; its rule is filed under the "
     "call `index_into(x, i)`, whose body the operator is"},

    {"tensor:multiply",
     "mul_tensor",
     2,
     "`*/2` is already `:op/mul_d`'s key and one key holds one rule, so the array product's rule "
     "is filed under the call `mul_tensor(x, w)`"},
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

/// The spelling a conversion's destination is written with, from the letters its name ends in: `d`
/// is `float`, `f` is `f32`, `f6` is `f16`, `bf` is `bf16`, `c` and `z` are the two complex types.
/// These are the spellings the *declarations* use (`float(x: i32)`, `f32(x: float)`), so the key
/// this answers is the one a law's left side is filed under.
std::optional<std::string> spellingOfDestination(const std::string &letters) {
    struct Spelling {
        const char *letters;
        const char *head;
    };
    static const Spelling kSpellings[] = {
        {"bf", "bf16"}, {"f6", "f16"}, {"d", "float"}, {"f", "f32"}, {"c", "c64"}, {"z", "c128"},
    };
    for (const Spelling &spelling : kSpellings) {
        if (letters == spelling.letters) {
            return std::string(spelling.head);
        }
    }
    return std::nullopt;
}

/// The key of a conversion named `<source>to<destination>`: the destination's spelling at one
/// operand, and nothing else.
///
/// The *source* is deliberately not in the key. One spelling names several operators -- `float` at
/// one operand is `float(x: float)`, `float(x: int)`, `float(x: string)` and the rest -- so a key
/// that carried the source would be the index encoding a type, which is work the engine already
/// does: it instantiates a rule at the operand's own type and refuses by name when no instance is
/// made. The family files one rule per operand type under the spelling and `lookupRules` answers
/// them all; which of them answers is not the index's question.
std::optional<RuleKey> conversionKeyOf(const std::string &name, const camel::core::type::Type *result) {
    const size_t to = name.find("to");
    if (to == std::string::npos || to == 0 || to + 2 >= name.size()) {
        return std::nullopt;
    }
    // The key is the destination's spelling, and the destination of a conversion is the type of its
    // result: it is asked of the type the caller is holding, which is an instance fact, rather than
    // decoded out of the operator's name.
    if (result != nullptr) {
        return RuleKey{result->toString(), 1};
    }
    const auto head = spellingOfDestination(name.substr(to + 2));
    if (!head.has_value()) {
        return std::nullopt;
    }
    return RuleKey{*head, 1};
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

std::optional<RuleKey> ruleKeyOf(
    const std::string &family, const std::string &uri, const camel::core::type::Type *result) {
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
        // A conversion: `itod` is `float(x: i32)` and `dtod` is `float(x: float)` -- one spelling at
        // one operand, seven sources. The key is the destination's spelling; the source is the
        // engine's business, not the index's.
        if (const auto conversion = conversionKeyOf(uri.substr(kLanguageOperator.size()), result)) {
            return conversion;
        }
        // Another `:op/` name -- a call of a function (`:op/fail`), a variable read
        // (`:op/var/get`), an operator of the core no ruleset writes a rule for. No key, no guess.
        return std::nullopt;
    }
    // A module's operator: its rule's left side is written as a call of its own name
    // (`matmul(x, w)`, `sum_axis(xs, axis, keepdims)`), and how many operands that takes is what the
    // family's index holds for that name.
    const std::string name = nameOf(uri);
    const auto arity       = camel::parse::ast::arityOfHead(family, name);
    if (!arity.has_value()) {
        return std::nullopt;
    }
    return RuleKey{name, *arity};
}

std::optional<camel::parse::ast::RuleMatch> forwardRuleOf(const std::string &uri, const camel::core::type::Type *result) {
    return ruleIn("rulesets.jvp", ruleKeyOf("rulesets.jvp", uri, result));
}

std::vector<camel::parse::ast::RuleMatch> forwardRulesOf(const std::string &uri, const camel::core::type::Type *result) {
    std::vector<camel::parse::ast::RuleMatch> answers;
    const auto key = ruleKeyOf("rulesets.jvp", uri, result);
    if (!key.has_value()) {
        return answers;
    }
    const std::vector<std::string> names =
        camel::parse::ast::lookupRules("rulesets.jvp", key->head, key->arity);
    answers.reserve(names.size());
    for (const std::string &name : names) {
        answers.push_back(camel::parse::ast::RuleMatch{camel::parse::ast::RuleLookup::Found, name});
    }
    return answers;
}

std::optional<camel::parse::ast::RuleMatch> transposeRuleOf(
    const std::string &uri, const camel::core::type::Type *result) {
    return ruleIn("rulesets.transpose", ruleKeyOf("rulesets.transpose", uri, result));
}

std::optional<camel::parse::ast::RuleMatch> vmapRuleOf(const std::string &uri, const camel::core::type::Type *result) {
    return ruleIn("rulesets.vmap", ruleKeyOf("rulesets.vmap", uri, result));
}

} // namespace camel::core