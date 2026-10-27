// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/type/resolver.h"

#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/typevar.h"
#include "camel/utils/assert.h"

#include <cctype>

using namespace camel::core::type;

namespace {

/// The text without the blanks at either end.
std::string trimmed(const std::string &text) {
    const size_t first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(" \t\r\n") - first + 1);
}

/// The index of the bracket that closes the one at `open`, or npos. The three bracket kinds nest
/// inside one another and each list is balanced, so counting the opening bracket's own character
/// finds its partner (`<m: Map<K, V>>` closes at the last `>`).
size_t closingBracketOf(const std::string &text, size_t open) {
    const char close = text[open] == '(' ? ')' : text[open] == '[' ? ']' : text[open] == '{' ? '}' : '>';
    int depth        = 0;
    for (size_t i = open; i < text.size(); ++i) {
        if (text[i] == '=' && i + 1 < text.size() && text[i + 1] == '>') {
            ++i; // the `>` of `=>` closes nothing
            continue;
        }
        if (text[i] == text[open]) {
            ++depth;
        } else if (text[i] == close && --depth == 0) {
            return i;
        }
    }
    return std::string::npos;
}

/// The index of the `=>` that separates a signature's parameters from its result: the last one
/// outside every bracket group. The `=>` of a function type written as a parameter
/// (`<f: (acc: U, item: T) => U>`) is inside a group and is not the one.
size_t resultArrowOf(const std::string &text) {
    int depth       = 0;
    size_t arrow    = std::string::npos;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '=' && i + 1 < text.size() && text[i + 1] == '>') {
            if (depth == 0) {
                arrow = i;
            }
            ++i;
            continue;
        }
        if (c == '(' || c == '[' || c == '{' || c == '<') {
            ++depth;
        } else if (c == ')' || c == ']' || c == '}' || c == '>') {
            --depth;
        }
    }
    return arrow;
}

/// The name the parameter `text` writes at its head: `key: K`, `...rest: U...`, `args?: (...)`,
/// `var n: int`. Empty when it writes none, or writes a parameter that is never given by name
/// (a rest parameter collects the arguments left over at the call).
std::string parameterNameOf(const std::string &text) {
    const std::string part = trimmed(text);
    if (part.empty() || part.starts_with("...")) {
        return {};
    }
    size_t at = part.starts_with("var ") ? 4 : 0;
    while (at < part.size() && std::isspace(static_cast<unsigned char>(part[at]))) {
        ++at;
    }
    const size_t start = at;
    if (at >= part.size() || !(std::isalpha(static_cast<unsigned char>(part[at])) || part[at] == '_')) {
        return {};
    }
    while (at < part.size() &&
           (std::isalnum(static_cast<unsigned char>(part[at])) || part[at] == '_')) {
        ++at;
    }
    const std::string name = part.substr(start, at - start);
    while (at < part.size() && std::isspace(static_cast<unsigned char>(part[at]))) {
        ++at;
    }
    if (at < part.size() && part[at] == '?') {
        ++at;
        while (at < part.size() && std::isspace(static_cast<unsigned char>(part[at]))) {
            ++at;
        }
    }
    return at < part.size() && part[at] == ':' ? name : std::string();
}

/// The names of the parameters written between brackets, split at the commas that are not nested:
/// false when one of them writes no name.
bool namesOfList(const std::string &text, std::vector<std::string> &names) {
    size_t at = 0;
    for (;;) {
        int depth         = 0;
        size_t comma      = std::string::npos;
        for (size_t i = at; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '=' && i + 1 < text.size() && text[i + 1] == '>') {
                ++i;
                continue;
            }
            if (c == '(' || c == '[' || c == '{' || c == '<') {
                ++depth;
            } else if (c == ')' || c == ']' || c == '}' || c == '>') {
                --depth;
            } else if (c == ',' && depth == 0) {
                comma = i;
                break;
            }
        }
        const std::string part = trimmed(text.substr(at, comma == std::string::npos ? comma : comma - at));
        if (part.empty()) {
            if (comma == std::string::npos) {
                return true; // an empty list
            }
            return false; // a parameter left out
        }
        const std::string name = parameterNameOf(part);
        if (name.empty()) {
            return false;
        }
        names.push_back(name);
        if (comma == std::string::npos) {
            return true;
        }
        at = comma + 1;
    }
}

/// The declared signature as an operator's type: closed, with its parameter names, var marks
/// kept.
FunctionType *closedSignature(FunctionType *declared) {
    ASSERT(declared->hasExitType(), "A declared operator has a result type");
    FunctionTypeFactory factory;
    for (size_t i = 0; i < declared->withTypesCount(); ++i) {
        factory.addWithArg(
            std::string(declared->argNameAt(i)), declared->withTypeAt(i),
            declared->withIsVarAt(i));
    }
    for (size_t i = 0; i < declared->normTypesCount(); ++i) {
        factory.addNormArg(
            std::string(declared->argNameAt(declared->withTypesCount() + i)),
            declared->normTypeAt(i), declared->normIsVarAt(i));
    }
    factory.setExitType(declared->exitType());
    factory.setResources(declared->resources());
    return factory.build();
}

} // namespace

bool camel::core::type::namesOfSignature(
    const FunctionType *funcType, std::vector<std::string> &with, std::vector<std::string> &norm) {
    if (funcType == nullptr || !funcType->hasArgNames()) {
        return false;
    }
    std::vector<std::string> withNames, normNames;
    for (size_t i = 0; i < funcType->withTypesCount(); ++i) {
        const std::string name(funcType->argNameAt(i));
        if (name.empty()) {
            return false;
        }
        withNames.push_back(name);
    }
    for (size_t i = 0; i < funcType->normTypesCount(); ++i) {
        const std::string name(funcType->argNameAt(funcType->withTypesCount() + i));
        if (name.empty()) {
            return false;
        }
        normNames.push_back(name);
    }
    with = std::move(withNames);
    norm = std::move(normNames);
    return true;
}

bool camel::core::type::namesOfSignatureText(
    const std::string &text, std::vector<std::string> &with, std::vector<std::string> &norm) {
    const size_t arrow = resultArrowOf(text);
    if (arrow == std::string::npos) {
        return false; // not a signature: nothing says which parameters it has
    }
    const std::string head = text.substr(0, arrow);
    size_t at              = 0;
    std::vector<std::string> withNames, normNames;
    if (at < head.size() && head[at] == '<') {
        const size_t close = closingBracketOf(head, at);
        if (close == std::string::npos || !namesOfList(head.substr(at + 1, close - at - 1), withNames)) {
            return false;
        }
        at = close + 1;
    }
    while (at < head.size() && std::isspace(static_cast<unsigned char>(head[at]))) {
        ++at;
    }
    if (at >= head.size() || head[at] != '(') {
        return false;
    }
    const size_t close = closingBracketOf(head, at);
    if (close == std::string::npos || !namesOfList(head.substr(at + 1, close - at - 1), normNames)) {
        return false;
    }
    with = std::move(withNames);
    norm = std::move(normNames);
    return true;
}

DeclaredFuncTypeResolver::DeclaredFuncTypeResolver(FunctionType *declared)
    : funcType_(closedSignature(declared)) {
    for (Type *param : funcType_->withTypesSpan()) {
        collectBindableTypeVars(param, bindable_);
    }
    for (Type *param : funcType_->normTypesSpan()) {
        collectBindableTypeVars(param, bindable_);
    }
    collectBindableTypeVars(funcType_->exitType(), bindable_);
    generic_ = !bindable_.empty();
}

std::optional<FunctionType *> DeclaredFuncTypeResolver::resolve(
    const type_vec_t &with, const type_vec_t &norm) const {
    if (with.size() != funcType_->withTypesCount() || norm.size() != funcType_->normTypesCount()) {
        return std::nullopt; // reject
    }
    if (!generic_) {
        for (size_t i = 0; i < with.size(); ++i) {
            if (!funcType_->withTypeAt(i)->assignableFrom(valueTypeOf(with[i]))) {
                return std::nullopt;
            }
        }
        for (size_t i = 0; i < norm.size(); ++i) {
            if (!funcType_->normTypeAt(i)->assignableFrom(valueTypeOf(norm[i]))) {
                return std::nullopt;
            }
        }
        return funcType_;
    }
    // The variables are what the arguments say they are; a call that leaves one open is not a
    // use of the operator (nothing decides the code it runs on), so it is rejected here and
    // reported at the call as the mismatch it is. An argument that states no element type is one
    // of those: the empty literal `[]` describes the variable standing at its element type as
    // `void`, and `void` is the "no value" marker rather than an element type, so the position the
    // literal stands in is not one that can fix the variable (design principles 5.1, 5.2). A
    // variable that no parameter stands a type at is another question, answered where it is
    // bound: a callback that gives nothing is a value of `void`, not an element type nobody
    // stated.
    for (size_t i = 0; i < with.size(); ++i) {
        if (!unstatedBindingOf(funcType_->withTypeAt(i), valueTypeOf(with[i])).empty()) {
            return std::nullopt;
        }
    }
    for (size_t i = 0; i < norm.size(); ++i) {
        if (!unstatedBindingOf(funcType_->normTypeAt(i), valueTypeOf(norm[i])).empty()) {
            return std::nullopt;
        }
    }
    type_bindings_t bindings;
    for (size_t i = 0; i < with.size(); ++i) {
        if (!unifyTypeVars(funcType_->withTypeAt(i), valueTypeOf(with[i]), bindings)) {
            return std::nullopt;
        }
    }
    for (size_t i = 0; i < norm.size(); ++i) {
        if (!unifyTypeVars(funcType_->normTypeAt(i), valueTypeOf(norm[i]), bindings)) {
            return std::nullopt;
        }
    }
    for (const std::string &name : bindable_) {
        if (findTypeBinding(bindings, name) == nullptr) {
            return std::nullopt;
        }
    }
    auto *concrete = static_cast<FunctionType *>(substituteTypeVars(funcType_, bindings));
    for (size_t i = 0; i < with.size(); ++i) {
        if (!concrete->withTypeAt(i)->assignableFrom(valueTypeOf(with[i]))) {
            return std::nullopt;
        }
    }
    for (size_t i = 0; i < norm.size(); ++i) {
        if (!concrete->normTypeAt(i)->assignableFrom(valueTypeOf(norm[i]))) {
            return std::nullopt;
        }
    }
    return concrete;
}

std::optional<FunctionType *> DeclaredFuncTypeResolver::resolveForCall(
    const type_vec_t &types, Type *expectedResult, const type_vec_t &with, const type_vec_t &norm,
    static_args_t normStatics) const {
    (void)expectedResult;
    (void)normStatics;
    if (types.empty()) {
        return resolve(with, norm);
    }
    // The call writes the declaration's own variables (`show::<f32>([])`): they are fixed by
    // position, the way a generic function's `::<...>` fixes its head parameters, and the
    // arguments are then read against the declaration with them already written in. A rule
    // without variables takes no type argument.
    if (!generic_ || types.size() != bindable_.size()) {
        return std::nullopt;
    }
    type_bindings_t bindings;
    for (size_t i = 0; i < types.size(); ++i) {
        bindings.emplace_back(bindable_[i], types[i]);
    }
    auto *concrete = static_cast<FunctionType *>(substituteTypeVars(funcType_, bindings));
    // What is left is the ordinary resolution, against the declaration with those variables
    // written in: the variables the explicit types did not write are read from the arguments,
    // and an argument that states none of them -- the empty literal `[]` -- is refused here like
    // any other mismatch. Its answer is the instance, so a call that says `::<f32>` is not asked
    // again what it already said.
    const type_vec_t withTypes(concrete->withTypesSpan().begin(), concrete->withTypesSpan().end());
    const type_vec_t normTypes(concrete->normTypesSpan().begin(), concrete->normTypesSpan().end());
    if (const auto instance = resolve(withTypes, normTypes); instance.has_value()) {
        return instance;
    }
    // A variable only the result mentions (`T[..]`) is stated by the call alone: no parameter
    // binds it, so the instance the types write is the rule's answer.
    std::vector<std::string> inParameters;
    for (Type *param : funcType_->withTypesSpan()) {
        collectBindableTypeVars(param, inParameters);
    }
    for (Type *param : funcType_->normTypesSpan()) {
        collectBindableTypeVars(param, inParameters);
    }
    if (inParameters.size() == bindable_.size()) {
        return std::nullopt;
    }
    return concrete;
}

std::optional<FunctionType *> StaticFuncTypeResolver::resolve(
    const type_vec_t &with, const type_vec_t &norm) const {
    ASSERT(funcType_, "FunctionType is null");
    const size_t withCount = funcType_->withTypesCount();
    const size_t normCount = funcType_->normTypesCount();
    if (withCount != with.size() || normCount != norm.size()) {
        return std::nullopt; // reject
    }
    // A parameter type is the type of the value; the argument of a `var` parameter is the
    // variable holding it.
    for (size_t i = 0; i < withCount; i++) {
        if (!funcType_->withTypeAt(i)->assignableFrom(valueTypeOf(with[i]))) {
            return std::nullopt; // reject
        }
    }
    for (size_t i = 0; i < normCount; i++) {
        if (!funcType_->normTypeAt(i)->assignableFrom(valueTypeOf(norm[i]))) {
            return std::nullopt; // reject
        }
    }
    return funcType_; // accept
}

std::optional<FunctionType *> DynamicFuncTypeResolver::resolve(
    const type_vec_t &withArgs, const type_vec_t &normArgs) const {
    // A caller that holds no constants asks the rule the same question without them; a rule whose
    // result follows their values answers what it can without them.
    return resolveWith(withArgs, normArgs, {});
}

std::optional<FunctionType *> DynamicFuncTypeResolver::resolveWith(
    const type_vec_t &withArgs, const type_vec_t &normArgs, static_args_t normStatics) const {
    if (withVars_.first != -1 && static_cast<int>(withArgs.size()) != withVars_.first) {
        return std::nullopt; // reject
    }
    if (normVars_.first != -1 && static_cast<int>(normArgs.size()) != normVars_.first) {
        return std::nullopt; // reject
    }
    // A `var` parameter is typed by the value; its argument may be the variable holding it.
    const auto isVar = [](const var_declare_t &vars, size_t i) {
        return static_cast<int>(i) < vars.first ? bool(vars.second[i]) : false;
    };
    type_vec_t with = withArgs, norm = normArgs;
    for (size_t i = 0; i < with.size(); i++) {
        with[i] = isVar(withVars_, i) ? valueTypeOf(with[i]) : with[i];
    }
    for (size_t i = 0; i < norm.size(); i++) {
        norm[i] = isVar(normVars_, i) ? valueTypeOf(norm[i]) : norm[i];
    }
    // The rule's own callback, and the one that also reads the call's constants when the rule
    // declared it: a rule whose result type follows the values the program wrote answers from the
    // statics path, and a rule that declared none answers exactly as it did.
    auto optResType = staticsResolver_ ? staticsResolver_(with, norm, normStatics)
                                       : resolver_(with, norm);
    if (!optResType) {
        return std::nullopt; // reject
    }
    param_vec_t withParams, normParams;
    for (size_t i = 0; i < with.size(); i++) {
        withParams.emplace_back(with[i], isVar(withVars_, i));
    }
    for (size_t i = 0; i < norm.size(); i++) {
        normParams.emplace_back(norm[i], isVar(normVars_, i));
    }
    // The same call shape resolves to the same signature object: a program resolves the same
    // few operator signatures at every call, and the permanent space never frees.
    return FunctionType::create(withParams, normParams, *optResType);
}
