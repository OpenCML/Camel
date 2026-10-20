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
 * Author: Zhenjie Wei
 * Created: Oct. 03, 2024
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

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
        const param_init_list_t &withTypes, const param_init_list_t &normTypes, Type *returnType,
        const ModifierSet &modifiers = Modifier::None) {
        return std::make_unique<StaticFuncTypeResolver>(
            FunctionType::create(withTypes, normTypes, returnType, modifiers));
    }

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
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

    /// The declared number of with- and ordinary parameters; -1 for any number.
    int withCount() const { return withVars_.first; }
    int normCount() const { return normVars_.first; }

  private:
    std::string signature_;
    FunctionType *fixed_ = nullptr;
    var_declare_t withVars_, normVars_;
    ResolverFunc resolver_;
};

} // namespace camel::core::type
