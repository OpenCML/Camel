// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * OpRegistry: storage of OpDefs and derivation of operator groups and kernel
 * maps. The resolver built for each OpDef performs the generic part of
 * overload matching (arity and parameter kinds) and then defers to the
 * OpDef's inference function, passing the values of constant arguments.
 */

#include "registry.h"

#include "op_stats.h"

#include "indexing.h"

#include "../dtype.h"
#include "camel/core/derivative.h"
#include "camel/core/operator_traits.h"
#include "camel/core/type/bytes.h"
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
    // A tensor is a numeric array, and the alias `Tensor` is one spelling of it, so the kind of
    // an argument is read through the one predicate that answers for both.
    const bool tensor = isTensorType(type);
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
    case ParamKind::Bytes:
        return type && BytesType::get()->equals(type);
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
        return FunctionType::create(param_vec_t{}, normParams, *result, modifiers);
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
        const StaticFacts facts(norm, normStatics);
        return def_->infer(InferContext(norm, facts.constants, facts.partials));
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
        camel::core::OperatorTable::instance().set(
            uri,
            {.pure = def.traits.pure, .elementwise = def.traits.elementwise});
        if (def.semantics) {
            // After the traits, which the declaration is checked against. A rejected
            // declaration is a defect of this module: say so with the module's name.
            core::sem::OperatorSemantics semantics = *def.semantics;
            if (semantics.key.name == core::kEmptySym) {
                semantics.key =
                    core::sem::OperatorKey::of(uri, semantics.key.major, semantics.key.minor);
            }
            if (def.index) {
                semantics.indexing = indexingFrom(def.index);
            }
            try {
                camel::core::OperatorTable::instance().declareSemantics(uri, std::move(semantics));
            } catch (const std::invalid_argument &e) {
                throw std::logic_error("module " + std::string(protocol) + ": " + e.what());
            }
        }
        if (def.vjp) {
            camel::core::DerivativeRegistry::instance().setRule(uri, def.vjp);
        }
        if (def.foldFromTypes) {
            camel::core::OperatorTable::instance().setTypeFolder(
                uri,
                [fold = def.foldFromTypes](
                    std::span<Type *const>
                        types,
                    static_args_t statics,
                    mm::IAllocator &allocator) -> std::optional<slot_t> {
                    const StaticFacts facts(types, statics);
                    try {
                        return fold(
                            InferContext(types, facts.constants, facts.partials),
                            allocator);
                    } catch (const std::exception &) {
                        return std::nullopt; // left to the program to report when it runs
                    }
                });
        }
        entries_.push_back(
            Entry{
                std::string(protocol),
                std::move(uri),
                std::make_shared<const OpDef>(std::move(def))});
        // An operator without an export name belongs to no operator group, which is what
        // publishes resolvers by URI. Such an operator still needs its result type to follow its
        // arguments when types are re-inferred (specialization to an export's input shapes);
        // otherwise everything downstream of it loses its static shape.
        const Entry &entry = entries_.back();
        byId_.emplace(
            camel::core::OperatorTable::instance().intern(entry.uri),
            entries_.size() - 1);
        if (entry.def->exports.empty() && entry.def->infer) {
            camel::core::OperatorTable::instance().setResolver(
                entry.uri,
                std::make_shared<OpDefResolver>(entry.def));
        }
    }
}

const OpDef *OpRegistry::find(std::string_view uri) const {
    std::lock_guard guard(mutex_);
    auto it = byUri_.find(std::string(uri));
    return it == byUri_.end() ? nullptr : entries_[it->second].def.get();
}
const OpDef *OpRegistry::find(core::OperId id) const {
    std::lock_guard guard(mutex_);
    auto it = byId_.find(id);
    return it == byId_.end() ? nullptr : entries_[it->second].def.get();
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
            kernels.emplace(
                name,
                opStatsEnabled() ? timedKernel(name, entry.def->kernel) : entry.def->kernel);
        }
    }
    return kernels;
}

} // namespace camel::tensor::ops
