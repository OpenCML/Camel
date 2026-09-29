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
 * Updated: Apr. 10, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"

#include <functional>
#include <optional>
#include <span>
#include <string>

namespace camel::core::type {

using ResolverFunc = std::function<std::optional<Type *>(
    const type_vec_t &, const type_vec_t &, const ModifierSet &)>;

/// Compile-time values of a call's norm arguments, aligned with their types: the slot of each
/// argument that is a constant (a literal or other static data), nullopt otherwise.
using static_args_t = std::span<const std::optional<slot_t>>;

class FuncTypeResolver {
  public:
    virtual ~FuncTypeResolver() = default;

    virtual std::optional<FunctionType *>
    resolve(const type_vec_t &with, const type_vec_t &norm, const ModifierSet &modifiers) const = 0;

    virtual std::string signature() const = 0;

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

    /**
     * Why resolveWith() rejects these arguments, when the resolver can tell something more
     * precise than "no match": for example, the argument kinds fit but statically known tensor
     * shapes conflict. Returns nullopt when the arguments simply do not fit this overload.
     */
    virtual std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const {
        (void)with;
        (void)norm;
        (void)normStatics;
        (void)modifiers;
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
        : signature_(std::move(signature)), resolver_(std::move(resolver)) {
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

  private:
    std::string signature_;
    var_declare_t withVars_, normVars_;
    ResolverFunc resolver_;
};

} // namespace camel::core::type
