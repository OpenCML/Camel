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
 * Updated: Nov. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "base.h"
#include "composite/func.h"

#include <functional>
#include <optional>

using ResolverFunc = std::function<std::optional<type_ptr_t>(
    const type_vec_t &, const type_vec_t &, const ModifierSet &)>;

class FuncTypeResolver {
  public:
    virtual ~FuncTypeResolver() = default;

    virtual std::optional<func_type_ptr_t>
    resolve(const type_vec_t &with, const type_vec_t &norm, const ModifierSet &modifiers) const = 0;

    virtual std::string signature() const = 0;
};

using resolver_ptr_t = std::shared_ptr<FuncTypeResolver>;

class StaticFuncTypeResolver : public FuncTypeResolver {
  public:
    StaticFuncTypeResolver(const func_type_ptr_t &funcType) : funcType_(funcType) {}
    StaticFuncTypeResolver(const func_type_ptr_t &&funcType) : funcType_(std::move(funcType)) {}

    static resolver_ptr_t create(const func_type_ptr_t &funcType) {
        return std::make_unique<StaticFuncTypeResolver>(funcType);
    }
    static resolver_ptr_t create(const func_type_ptr_t &&funcType) {
        return std::make_unique<StaticFuncTypeResolver>(std::move(funcType));
    }
    static resolver_ptr_t create(
        const param_init_list_t &withTypes, const param_init_list_t &normTypes,
        const type_ptr_t &returnType, const ModifierSet &modifiers = Modifier::None) {
        return std::make_unique<StaticFuncTypeResolver>(
            FunctionType::create(withTypes, normTypes, returnType, modifiers));
    }

    std::optional<func_type_ptr_t> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override;

    std::string signature() const override { return funcType_->toString(); }

  private:
    func_type_ptr_t funcType_;
};

class DynamicFuncTypeResolver : public FuncTypeResolver {
  public:
    // int表示变量个数，vector<bool>表示是否为可变参数
    // int(-1) 表示不限制变量个数
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

    std::optional<func_type_ptr_t> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override;

    std::string signature() const override { return signature_; }

  private:
    std::string signature_;
    var_declare_t withVars_, normVars_;
    ResolverFunc resolver_;
};
