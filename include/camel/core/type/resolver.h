// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace camel::core::type {

using ResolverFunc = std::function<std::optional<Type *>(
    const type_vec_t &, const type_vec_t &, const ModifierSet &)>;

/**
 * What is known at compile time about the value of one argument of a call.
 *
 * `value` is the slot of an argument that is a constant: a literal or other static data, or the
 * result of an operator whose argument types fix it (its type folder). `elements` is
 * set for an array written in place, such as `[n, 3]`: its length is the array's length, and
 * each element is the slot of that element when it is a constant, nullopt otherwise. Both
 * only refine a result type; they never decide whether the arguments fit.
 */
struct StaticArg {
    std::optional<slot_t> value;
    std::optional<std::vector<std::optional<slot_t>>> elements;

    StaticArg() = default;
    StaticArg(std::nullopt_t) {}
    StaticArg(std::optional<slot_t> v) : value(v) {}
    StaticArg(slot_t v) : value(v) {}

    /// As the optional this struct replaced: true when the whole argument is a constant.
    explicit operator bool() const { return value.has_value(); }
    slot_t operator*() const { return *value; }
};

/// Compile-time knowledge of a call's norm arguments, aligned with their types.
using static_args_t = std::span<const StaticArg>;

/**
 * A result callback that also reads what the compiler knows about the call's constant arguments:
 * the path a rule whose result type depends on their values needs (`slice(t, axis, a, b)` states
 * the length it takes when the bounds are constants, design principles 5.4). A rule that declares
 * one answers from it whenever the caller has the statics -- `resolveWith`, which every call site
 * with constants reaches -- and keeps the plain callback above for a caller that has none.
 */
using ResolverWithStaticsFunc = std::function<std::optional<Type *>(
    const type_vec_t &, const type_vec_t &, static_args_t, const ModifierSet &)>;

/// The names a signature gives its parameters, the `with` ones first and the ordinary ones after;
/// false when it records no name for some position (see FuncTypeResolver::parameterNames).
bool namesOfSignature(
    const FunctionType *funcType, std::vector<std::string> &with, std::vector<std::string> &norm);

/// The names the parameters of a written signature spell (`"<key: K> (m: Map<K, V>) => V?"`), the
/// `with` list first and the ordinary one after; false when the text is not a signature with a
/// name on every parameter. The text is the rule's own spelling, the one `inspect --ops` prints.
bool namesOfSignatureText(
    const std::string &text, std::vector<std::string> &with, std::vector<std::string> &norm);

class FuncTypeResolver {
  public:
    virtual ~FuncTypeResolver() = default;

    virtual std::optional<FunctionType *>
    resolve(const type_vec_t &with, const type_vec_t &norm, const ModifierSet &modifiers) const = 0;

    virtual std::string signature() const = 0;

    /// Which parameters of the overload are `var`, when its parameter counts are fixed: the
    /// with parameters, then the ordinary ones. False when it cannot be told (a variadic or
    /// computed signature). The resource declaration of the operator is checked against it.
    virtual bool varParameters(std::vector<bool> &with, std::vector<bool> &norm) const {
        (void)with;
        (void)norm;
        return false;
    }

    /**
     * The one signature this rule accepts, when it does not depend on the arguments: the
     * rule is then exactly that function type. nullptr for a rule that accepts a family of
     * types (its result is computed from the argument types). Whether an overload of the
     * program is "more specific" than a rule of an operator is decided from this declaration
     * and nothing else (design principles 4.3, log 2.17).
     */
    virtual FunctionType *fixedSignature() const { return nullptr; }

    /**
     * The signature the rule declares, type variables and all: the same function type as
     * fixedSignature() for a rule without variables, and the declared form for one that is a
     * family. nullptr for a rule whose signature is computed rather than declared. It is what a
     * reader asks about the parameters themselves -- which of them mentions a type variable --
     * where fixedSignature() cannot answer, a family having no one signature.
     */
    virtual FunctionType *declaredSignature() const { return nullptr; }

    /**
     * The names this rule gives its parameters, the `with` ones first and the ordinary ones
     * after: what a named argument binds to (`len(x: xs)`). A rule whose signature is fixed
     * answers from it; a rule whose result follows its arguments has no signature object to
     * answer from, and reads the parameters its written signature spells -- the very text
     * `inspect --ops` prints and `--verify-ops` checks against the declaration file, so the name
     * a call binds to and the name the reference documents are one. False when the rule gives
     * some parameter no name, leaves a count unconstrained (a variadic list is not a position),
     * or has a `rest`/`constas` parameter (those are written at their own position, never by
     * name): such a rule is one a call's name cannot reach, and the call is refused rather than
     * bound to a position the rule does not name.
     */
    virtual bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const {
        (void)with;
        (void)norm;
        return false;
    }

    /**
     * Resolution that may also use the values of constant arguments, for operators whose result
     * type depends on them (e.g. a tensor constructor whose shape is given by a shape literal).
     * The default ignores the values.
     */
    virtual std::optional<FunctionType *> resolveWith(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const {
        (void)normStatics;
        return resolve(with, norm, modifiers);
    }

    /// Types supplied with f::<T> and the expected result are compile-time context only.
    /// Ordinary resolvers reject explicit types; they never become with-argument values.
    virtual std::optional<FunctionType *> resolveForCall(
        const type_vec_t &types, Type *expectedResult, const type_vec_t &with,
        const type_vec_t &norm, static_args_t normStatics, const ModifierSet &modifiers) const {
        (void)expectedResult;
        return types.empty() ? resolveWith(with, norm, normStatics, modifiers) : std::nullopt;
    }

    /**
     * Why resolveWith() rejects these arguments, when the resolver can tell something more
     * precise than "no match": for example, the argument kinds fit but statically known tensor
     * shapes conflict. Returns nullopt when the arguments simply do not fit this overload.
     */
    virtual std::optional<std::string> explainCallRejection(
        const type_vec_t &types, Type *expectedResult, const type_vec_t &with,
        const type_vec_t &norm, static_args_t normStatics, const ModifierSet &modifiers) const {
        (void)expectedResult;
        return types.empty() ? explainRejection(with, norm, normStatics, modifiers) : std::nullopt;
    }

    virtual std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const {
        (void)with;
        (void)norm;
        (void)normStatics;
        (void)modifiers;
        return std::nullopt;
    }

    /**
     * The diagnostic that reports a rejection this rule explains, when the generic
     * ArgumentsRejected would not name the right thing. Its message takes the reason as its
     * only parameter. nullopt (the default) keeps ArgumentsRejected.
     */
    virtual std::optional<camel::core::error::SemanticDiag> rejectionDiag() const {
        return std::nullopt;
    }
};

using resolver_ptr_t = std::shared_ptr<FuncTypeResolver>;

class StaticFuncTypeResolver : public FuncTypeResolver {
  public:
    StaticFuncTypeResolver(FunctionType *funcType) : funcType_(funcType) {}

    static resolver_ptr_t create(FunctionType *funcType) {
        return std::make_unique<StaticFuncTypeResolver>(funcType);
    }
    static resolver_ptr_t create(
        const param_init_list_t &withTypes, const param_init_list_t &normTypes, Type *returnType) {
        return std::make_unique<StaticFuncTypeResolver>(
            FunctionType::create(withTypes, normTypes, returnType));
    }

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override;

    std::string signature() const override { return funcType_->toString(); }

    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        if (funcType_->hasRest() || funcType_->hasConstas()) {
            return false;
        }
        return namesOfSignature(funcType_, with, norm);
    }

    bool varParameters(std::vector<bool> &with, std::vector<bool> &norm) const override {
        for (size_t i = 0; i < funcType_->withTypesCount(); ++i) {
            with.push_back(funcType_->withIsVarAt(i));
        }
        for (size_t i = 0; i < funcType_->normTypesCount(); ++i) {
            norm.push_back(funcType_->normIsVarAt(i));
        }
        return true;
    }

    FunctionType *funcType() const { return funcType_; }
    FunctionType *fixedSignature() const override { return funcType_; }

  private:
    FunctionType *funcType_;
};

class DynamicFuncTypeResolver : public FuncTypeResolver {
  public:
    // int denotes parameter count, and vector<bool> indicates whether each parameter is variadic.
    // int(-1) means the parameter count is unconstrained.
    using var_declare_t = std::pair<int, std::vector<bool>>;
    DynamicFuncTypeResolver(
        const std::pair<var_declare_t, var_declare_t> &&vars, const std::string &&signature,
        const ResolverFunc &&resolver)
        : signature_(sourceTypeSpelling(signature)), resolver_(std::move(resolver)) {
        withVars_ = std::move(vars.first);
        normVars_ = std::move(vars.second);
        if (withVars_.first != -1 && withVars_.first != static_cast<int>(withVars_.second.size())) {
            ASSERT(false, "withVars size mismatch");
        }
        if (normVars_.first != -1 && normVars_.first != static_cast<int>(normVars_.second.size())) {
            ASSERT(false, "normVars size mismatch");
        }
    }

    static resolver_ptr_t create(
        const std::pair<var_declare_t, var_declare_t> &&vars, const std::string &&signature,
        const ResolverFunc &&resolver) {
        return std::make_unique<DynamicFuncTypeResolver>(
            std::move(vars),
            std::move(signature),
            std::move(resolver));
    }

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override;

    /// The same resolution with the call's constants in hand: a rule that declared a callback
    /// reading them (withStatics) answers from it, so its result type may follow the values the
    /// program wrote, and a rule that declared none answers as it always did.
    std::optional<FunctionType *> resolveWith(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const override;

    /// Declares that this rule's result may also follow the call's constant arguments, for an
    /// operator whose result type depends on their values. The plain callback above stays: it
    /// answers a caller that has no statics, and it is what the rule answers without them.
    DynamicFuncTypeResolver *withStatics(ResolverWithStaticsFunc resolver) {
        staticsResolver_ = std::move(resolver);
        return this;
    }

    std::string signature() const override { return signature_; }

    bool varParameters(std::vector<bool> &with, std::vector<bool> &norm) const override {
        if (withVars_.first == -1 || normVars_.first == -1) {
            return false;
        }
        with = withVars_.second;
        norm = normVars_.second;
        return true;
    }

    /// Declares that this rule accepts exactly one signature (a rule that adds a check, such
    /// as a shape check, to a signature that is otherwise fixed). Returns the resolver.
    DynamicFuncTypeResolver *declareFixedSignature(FunctionType *signature) {
        fixed_ = signature;
        return this;
    }
    FunctionType *fixedSignature() const override { return fixed_; }

    /// A rule that fixes its signature answers from it, as any fixed rule does. A rule whose
    /// result follows its arguments answers from the signature it writes, and only when that
    /// signature places every parameter: a count left unconstrained (a variadic list) or a
    /// `rest` parameter is not a position a name can mark.
    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        if (fixed_ != nullptr) {
            if (fixed_->hasRest() || fixed_->hasConstas()) {
                return false;
            }
            return namesOfSignature(fixed_, with, norm);
        }
        if (withVars_.first < 0 || normVars_.first < 0) {
            return false;
        }
        std::vector<std::string> writtenWith, writtenNorm;
        if (!namesOfSignatureText(signature_, writtenWith, writtenNorm)) {
            return false;
        }
        if (writtenWith.size() != static_cast<size_t>(withVars_.first) ||
            writtenNorm.size() != static_cast<size_t>(normVars_.first)) {
            // The rule spells parameters its declared counts do not agree with: the spelling is
            // not the rule's parameter list, and a name read from it would mark nothing.
            return false;
        }
        with = std::move(writtenWith);
        norm = std::move(writtenNorm);
        return true;
    }

    /// The declared number of with- and ordinary parameters; -1 for any number.
    int withCount() const { return withVars_.first; }
    int normCount() const { return normVars_.first; }

  private:
    std::string signature_;
    FunctionType *fixed_ = nullptr;
    var_declare_t withVars_, normVars_;
    ResolverFunc resolver_;
    /// The callback that also reads the call's constants; null for a rule whose result follows
    /// its argument types alone.
    ResolverWithStaticsFunc staticsResolver_;
};

/**
 * The rule of a body-less declaration: the signature written in a Camel module is the signature
 * the operator has (design principles 6.4, step 1). The declared type is the one
 * source of the parameters and the result, so the resolver accepts exactly what the declaration
 * says and nothing else:
 *
 *  - a signature without type variables is a fixed rule: an argument list of the declared
 *    shape whose argument types are assignable to the declared parameter types;
 *  - a signature with type parameters declared at its head is a family: the variables are bound from the argument
 *    types, every variable must be bound, and the resulting concrete signature is checked
 *    against the arguments. Each call therefore resolves to its own concrete signature while
 *    the operator URI stays one.
 *
 * The operator table takes the number of parameters from the declaration as well, so a
 * declaration is checked for overlapping rules by the ordinary operator-group registration.
 */
class DeclaredFuncTypeResolver : public FuncTypeResolver {
  public:
    explicit DeclaredFuncTypeResolver(FunctionType *declared);

    static resolver_ptr_t create(FunctionType *declared) {
        return std::make_unique<DeclaredFuncTypeResolver>(declared);
    }

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override;

    /// The declaration's own variables, fixed by the call's `::<...>` type arguments before the
    /// arguments are read: a variable the call writes is what the call is an instance of, so the
    /// arguments are matched against the declaration with those variables already bound
    /// (`show::<f32>([])`). A rule without variables takes no type argument, as an ordinary
    /// resolver does.
    std::optional<FunctionType *> resolveForCall(
        const type_vec_t &types, Type *expectedResult, const type_vec_t &with,
        const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const override;

    std::string signature() const override { return funcType_->toString(); }

    bool varParameters(std::vector<bool> &with, std::vector<bool> &norm) const override {
        for (size_t i = 0; i < funcType_->withTypesCount(); ++i) {
            with.push_back(funcType_->withIsVarAt(i));
        }
        for (size_t i = 0; i < funcType_->normTypesCount(); ++i) {
            norm.push_back(funcType_->normIsVarAt(i));
        }
        return true;
    }

    /// The signature as declared, closed (an operator's type is not a function still being
    /// built); null when the declaration has type variables, whose instances are not this rule.
    FunctionType *fixedSignature() const override { return generic_ ? nullptr : funcType_; }

    /// The declaration's own type, the variables still in it: a family's parameters are the ones
    /// the rule writes, and a call that leaves one of their variables without a type is a
    /// question about the declaration rather than about one of its instances.
    FunctionType *declaredSignature() const override { return funcType_; }

    /// A declaration names every parameter it writes, whatever its signature resolves to: the
    /// names are the declaration's own, and a named call reads them from here.
    bool parameterNames(std::vector<std::string> &with, std::vector<std::string> &norm)
        const override {
        if (funcType_->hasRest() || funcType_->hasConstas()) {
            return false;
        }
        return namesOfSignature(funcType_, with, norm);
    }

    /// The declared signature, with its parameter names.
    FunctionType *funcType() const { return funcType_; }
    size_t withCount() const { return funcType_->withTypesCount(); }
    size_t normCount() const { return funcType_->normTypesCount(); }

  private:
    FunctionType *funcType_;
    /// The names of the type variables the declaration binds; empty for a fixed signature.
    std::vector<std::string> bindable_;
    bool generic_ = false;
};

} // namespace camel::core::type
