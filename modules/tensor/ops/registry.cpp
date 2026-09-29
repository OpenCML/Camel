/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * OpRegistry: storage of OpDefs and derivation of operator groups and kernel
 * maps. The resolver built for each OpDef performs the generic part of
 * overload matching (arity and parameter kinds) and then defers to the
 * OpDef's inference function, passing the values of constant arguments.
 */

#include "registry.h"

#include "op_stats.h"

#include "../dtype.h"
#include "camel/core/derivative.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type/resolver.h"
#include "support.h"

#include <stdexcept>

namespace camel::tensor::ops {

using namespace camel::core::type;

namespace {

bool isNumericScalar(Type *type) { return type && isSupportedTensorScalar(type->code()); }

bool isIntScalar(Type *type) {
    return type && (type->code() == TypeCode::Int32 || type->code() == TypeCode::Int64);
}

bool isIntArray(Type *type) {
    if (!type || type->code() != TypeCode::Array) {
        return false;
    }
    return isIntScalar(static_cast<ArrayType *>(type)->elemType());
}

bool matchesKind(ParamKind kind, Type *type) {
    const bool tensor = asTensorType(type) != nullptr;
    switch (kind) {
    case ParamKind::Tensor:
        return tensor;
    case ParamKind::TensorLike:
        return tensor || (type && type->code() == TypeCode::Array);
    case ParamKind::TensorOrScalar:
        return tensor || isNumericScalar(type);
    case ParamKind::TensorLikeOrScalar:
        return tensor || isNumericScalar(type) || (type && type->code() == TypeCode::Array);
    case ParamKind::Int:
        return isIntScalar(type);
    case ParamKind::Number:
        return isNumericScalar(type);
    case ParamKind::Bool:
        return type && type->code() == TypeCode::Bool;
    case ParamKind::String:
        return type && type->code() == TypeCode::String;
    case ParamKind::IntArray:
        return isIntArray(type);
    }
    return false;
}

/// True when the argument count and every argument's kind fit the definition's parameters.
bool argumentsFit(const OpDef &def, const type_vec_t &with, const type_vec_t &norm) {
    if (!with.empty() || norm.size() > def.params.size()) {
        return false;
    }
    for (size_t i = 0; i < def.params.size(); ++i) {
        if (i >= norm.size()) {
            if (!def.params[i].optional) {
                return false;
            }
            continue;
        }
        if (!matchesKind(def.params[i].kind, norm[i])) {
            return false;
        }
    }
    return true;
}

/**
 * Overload resolution for one OpDef: arity and parameter kinds are matched generically, then
 * the definition's inference computes the result type. Constant argument values (shape
 * literals, axes) are passed to inference, so results such as zeros([2, 3]) get a static shape.
 */
class OpDefResolver final : public FuncTypeResolver {
  public:
    explicit OpDefResolver(std::shared_ptr<const OpDef> def)
        : def_(std::move(def)), signature_(def_->signature()) {}

    std::optional<FunctionType *> resolve(
        const type_vec_t &with, const type_vec_t &norm,
        const ModifierSet &modifiers) const override {
        return resolveWith(with, norm, {}, modifiers);
    }

    std::optional<FunctionType *> resolveWith(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &modifiers) const override {
        if (!argumentsFit(*def_, with, norm)) {
            return std::nullopt;
        }
        std::optional<Type *> result;
        try {
            result = infer(norm, normStatics);
        } catch (const std::invalid_argument &) {
            return std::nullopt; // statically known shapes or dtypes conflict (see explain)
        }
        if (!result) {
            return std::nullopt;
        }
        param_vec_t normParams;
        for (Type *t : norm) {
            normParams.emplace_back(t, false);
        }
        return FunctionType::create(param_vec_t{}, std::move(normParams), *result, modifiers);
    }

    std::optional<std::string> explainRejection(
        const type_vec_t &with, const type_vec_t &norm, static_args_t normStatics,
        const ModifierSet &) const override {
        if (!argumentsFit(*def_, with, norm)) {
            return std::nullopt;
        }
        try {
            (void)infer(norm, normStatics);
        } catch (const std::invalid_argument &e) {
            return std::string(e.what());
        }
        return std::nullopt;
    }

    std::string signature() const override { return signature_; }

  private:
    std::optional<Type *> infer(const type_vec_t &norm, static_args_t normStatics) const {
        std::vector<std::optional<ConstArg>> constants(norm.size());
        for (size_t i = 0; i < norm.size() && i < normStatics.size(); ++i) {
            if (normStatics[i]) {
                constants[i] = constArgOf(*normStatics[i], norm[i]);
            }
        }
        return def_->infer(InferContext(norm, constants));
    }

    std::shared_ptr<const OpDef> def_;
    std::string signature_;
};

} // namespace

OpRegistry &OpRegistry::instance() {
    static OpRegistry registry;
    return registry;
}

void OpRegistry::add(std::string_view protocol, std::vector<OpDef> defs) {
    std::lock_guard guard(mutex_);
    for (OpDef &def : defs) {
        std::string uri = std::string(protocol) + ":" + std::string(def.name);
        if (byUri_.contains(uri)) {
            throw std::logic_error("Operator registered twice: " + uri);
        }
        byUri_.emplace(uri, entries_.size());
        // Generic graph passes and autodiff engines see the definition through the core
        // registries.
        camel::core::OperatorTraitsRegistry::instance().set(
            uri,
            {.pure = def.traits.pure, .elementwise = def.traits.elementwise});
        if (def.vjp) {
            camel::core::DerivativeRegistry::instance().setRule(uri, def.vjp);
        }
        entries_.push_back(Entry{
            std::string(protocol),
            std::move(uri),
            std::make_shared<const OpDef>(std::move(def))});
    }
}

const OpDef *OpRegistry::find(std::string_view uri) const {
    std::lock_guard guard(mutex_);
    auto it = byUri_.find(std::string(uri));
    return it == byUri_.end() ? nullptr : entries_[it->second].def.get();
}

std::vector<oper_group_ptr_t> OpRegistry::operatorGroups(std::string_view protocol) const {
    std::lock_guard guard(mutex_);
    // Preserve the order in which export names first appear.
    std::vector<std::string> order;
    std::unordered_map<std::string, std::vector<std::pair<std::string, resolver_ptr_t>>> grouped;
    for (const Entry &entry : entries_) {
        if (entry.protocol != protocol) {
            continue;
        }
        for (std::string_view name : entry.def->exports) {
            std::string key(name);
            auto [it, inserted] = grouped.try_emplace(key);
            if (inserted) {
                order.push_back(key);
            }
            it->second.emplace_back(entry.uri, std::make_shared<OpDefResolver>(entry.def));
        }
    }
    std::vector<oper_group_ptr_t> groups;
    groups.reserve(order.size());
    for (const std::string &name : order) {
        groups.push_back(OperatorGroup::create(name, std::move(grouped[name])));
    }
    return groups;
}

std::unordered_map<std::string, operator_t> OpRegistry::kernelMap(std::string_view protocol) const {
    std::lock_guard guard(mutex_);
    std::unordered_map<std::string, operator_t> kernels;
    for (const Entry &entry : entries_) {
        if (entry.protocol == protocol) {
            const std::string name(entry.def->name);
            kernels.emplace(name, opStatsEnabled() ? timedKernel(name, entry.def->kernel) : entry.def->kernel);
        }
    }
    return kernels;
}

} // namespace camel::tensor::ops
