// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Explicit types are consumed while building concrete graphs, never passed as values.

#include "builder.h"
#include "builder_support.h"
#include "camel/core/data/primary.h"
#include "camel/utils/str.h"
#include <algorithm>
#include <unordered_set>
#include <utility>

namespace camel::compile::gir {
using namespace core::type;
using namespace core::error;

FunctionType *Builder::signatureForArgumentContext(node_handle_t target) const {
    if (const auto generic = genericDrefs_.find(target); generic != genericDrefs_.end()) {
        // A name with several generic functions gives the arguments a type only when every
        // overload declares the same parameters; a set whose entries differ says nothing about
        // which one a call means, exactly as an operator set whose entries differ does.
        const std::vector<generic_template_ptr_t> candidates = genericOverloadsOf(target);
        if (candidates.size() > 1) {
            const FunctionType *first = candidates.front()->funcType;
            for (const auto &candidate : candidates) {
                const FunctionType *other = candidate->funcType;
                if (other->withTypesCount() != first->withTypesCount() ||
                    other->normTypesCount() != first->normTypesCount()) {
                    return nullptr;
                }
                for (size_t i = 0; i < first->withTypesCount(); ++i) {
                    if (!other->withTypeAt(i)->equals(first->withTypeAt(i))) {
                        return nullptr;
                    }
                }
                for (size_t i = 0; i < first->normTypesCount(); ++i) {
                    if (!other->normTypeAt(i)->equals(first->normTypeAt(i))) {
                        return nullptr;
                    }
                }
            }
        }
        return candidates.empty() ? nullptr : candidates.front()->funcType;
    }
    if (nodeTypeOf(target)->code() == TypeCode::Function) {
        return static_cast<FunctionType *>(nodeTypeOf(target));
    }
    if (!nodeIsKind(target, runtime::GCNodeKind::Dref))
        return nullptr;
    const auto &named = nodeGraphOf(target)->drefTarget(target);
    if (std::holds_alternative<graph_vec_ptr_t>(named)) {
        const auto graphs = std::get<graph_vec_ptr_t>(named);
        if (graphs && graphs->size() == 1 && !decoratedOf_.contains(graphs->front().get())) {
            return graphs->front()->funcType();
        }
    } else if (std::holds_alternative<oper_group_ptr_t>(named)) {
        const auto ops = std::get<oper_group_ptr_t>(named);
        // The parameters of a native operator, when its signature does not depend on the call: an
        // overload set whose fixed entries declare the same parameters still has one signature to
        // give the arguments expected types (`p->run<[]>`: the empty array is a `Rule[]` because
        // the operator's `rules` parameter says so). A name whose entries differ, or whose
        // signature is computed from its arguments, gives none and its arguments are typed as
        // they were. Two entries with equal parameters are one signature even when the
        // declarations made two `FunctionType`s.
        const auto sameParameters = [](FunctionType *a, FunctionType *b) {
            if (a == nullptr || b == nullptr || a->withTypesCount() != b->withTypesCount() ||
                a->normTypesCount() != b->normTypesCount()) {
                return false;
            }
            for (size_t i = 0; i < a->withTypesCount(); ++i) {
                if (!a->withTypeAt(i)->equals(b->withTypeAt(i))) {
                    return false;
                }
            }
            for (size_t i = 0; i < a->normTypesCount(); ++i) {
                if (!a->normTypeAt(i)->equals(b->normTypeAt(i))) {
                    return false;
                }
            }
            return true;
        };
        FunctionType *same = nullptr;
        bool mixed         = false;
        for (const auto &[uri, resolver] : ops->resolvers()) {
            FunctionType *fixed = resolver ? resolver->fixedSignature() : nullptr;
            if (fixed == nullptr) {
                continue;
            }
            if (same == nullptr) {
                same = fixed;
            } else if (!sameParameters(same, fixed)) {
                mixed = true;
            }
        }
        if (same != nullptr && !mixed) {
            return same;
        }
    }
    return nullptr;
}

node_handle_t Builder::visitTypeApplication(const GCT::node_ptr_t &gct) {
    const bool outerAllowed = std::exchange(genericCalleeAllowed_, true);
    node_handle_t target{};
    try {
        target = valueNodeOf(visit(gct->at(0)));
    } catch (...) {
        genericCalleeAllowed_ = outerAllowed;
        throw;
    }
    genericCalleeAllowed_ = outerAllowed;
    // Positional, or all named `A = int` (design principles 5.9): in the tree a name is a
    // constant string right before its type.
    type_vec_t types;
    std::vector<std::string> given;
    for (size_t i = 1; i < gct->size(); ++i) {
        if (gct->at(i)->type() == GCT::NodeKind::DATA && i + 1 < gct->size()) {
            given.push_back(
                std::static_pointer_cast<core::data::StringData>(gct->at(i)->data())->data());
            ++i;
        }
        types.push_back(resolveTypeNode(gct->at(i)->dataType(), gct->at(i)));
    }
    if (const auto found = genericDrefs_.find(target); found != genericDrefs_.end()) {
        // The explicit types go to one function, so they choose among the overloads of the
        // name when it has several: the ones whose type parameters are exactly the names given
        // (design principles 4.4). A name with one generic function is that function.
        generic_template_ptr_t tmpl = found->second.one();
        if (const std::vector<generic_template_ptr_t> candidates = genericOverloadsOf(target);
            candidates.size() > 1) {
            std::vector<generic_template_ptr_t> fits;
            for (const auto &candidate : candidates) {
                const std::vector<std::string> names = typeParameterNames(candidate);
                if (names.size() != types.size()) {
                    continue;
                }
                if (given.empty()) {
                    fits.push_back(candidate); // positional: the count is what can be checked
                    continue;
                }
                std::unordered_set<std::string> declared(names.begin(), names.end());
                const bool allNamed = std::ranges::all_of(
                    given, [&](const std::string &name) { return declared.contains(name); });
                if (allNamed) {
                    fits.push_back(candidate);
                }
            }
            if (fits.size() != 1) {
                std::string listed;
                for (const auto &candidate : candidates) {
                    listed += (listed.empty() ? "" : ", ") + candidate->name +
                              candidate->funcType->toString();
                }
                diags_->of(SemanticDiag::GenericArgumentsMismatch)
                    .atOrigin(gct->origin())
                    .commit(
                        found->second.one()->name,
                        "explicit type arguments",
                        fits.empty() ? std::format(
                                           "no overload of the name takes these {} type "
                                           "argument(s); the candidates are {}",
                                           types.size(),
                                           listed)
                                     : std::format(
                                           "more than one overload takes these type arguments: "
                                           "{}",
                                           listed),
                        found->second.one()->funcType->toString());
                throw BuildAbortException();
            }
            tmpl = fits.front();
        }
        std::vector<std::string> names = typeParameterNames(tmpl);
        if (!given.empty()) {
            // By name: each type variable of the signature once, in any order.
            std::vector<Type *> byPosition(names.size(), nullptr);
            for (size_t i = 0; i < given.size(); ++i) {
                const auto at = std::ranges::find(names, given[i]);
                std::string problem;
                if (at == names.end())
                    problem = "'" + given[i] + "' is not a type parameter of " + tmpl->name;
                else if (byPosition[static_cast<size_t>(at - names.begin())] != nullptr)
                    problem = "type parameter '" + given[i] + "' is given twice";
                if (!problem.empty()) {
                    diags_->of(SemanticDiag::GenericArgumentsMismatch)
                        .atOrigin(gct->origin())
                        .commit(
                            tmpl->name,
                            "explicit type arguments",
                            problem + " (its type parameters: " +
                                strutil::join(names, ", ", [](const std::string &n) { return n; }) +
                                ")",
                            tmpl->funcType->toString());
                    throw BuildAbortException();
                }
                byPosition[static_cast<size_t>(at - names.begin())] = types[i];
            }
            for (size_t i = 0; i < names.size(); ++i) {
                if (byPosition[i] == nullptr) {
                    diags_->of(SemanticDiag::GenericArgumentsMismatch)
                        .atOrigin(gct->origin())
                        .commit(
                            tmpl->name,
                            "explicit type arguments",
                            "type parameter '" + names[i] + "' is not given",
                            tmpl->funcType->toString());
                    throw BuildAbortException();
                }
            }
            types = std::move(byPosition);
        }
        if (names.size() != types.size()) {
            diags_->of(SemanticDiag::GenericArgumentsMismatch)
                .atOrigin(gct->origin())
                .commit(
                    tmpl->name,
                    "explicit type arguments",
                    "expected " + std::to_string(names.size()) + " type argument(s), got " +
                        std::to_string(types.size()),
                    tmpl->funcType->toString());
            throw BuildAbortException();
        }
        type_bindings_t bindings;
        for (size_t i = 0; i < types.size(); ++i) {
            bindings.emplace_back(names[i], types[i]);
        }
        if (tmpl->funcType->hasConstas()) {
            diags_->of(SemanticDiag::ConstasFunctionNeedsCall)
                .atOrigin(gct->origin())
                .commit(tmpl->name);
            throw BuildAbortException();
        }
        if (const auto *when = GCT::Tree::of(tmpl->func)->whenOf(tmpl->func)) {
            const WhenResult verdict =
                evaluateWhenCondition(when->condition, tmpl->funcType, bindings, {}, tmpl);
            if (verdict.fails()) {
                diags_->of(SemanticDiag::WhenConditionUnmet)
                    .atOrigin(gct->origin())
                    .commit(
                        tmpl->name,
                        "explicit type arguments",
                        tmpl->name + " — when " + when->text);
                throw BuildAbortException();
            }
            if (!verdict.holds()) {
                reportWhenProblem(verdict, tmpl->name, when->condition);
            }
        }
        auto graph = instantiateGeneric(tmpl, bindings, {}, {}, gct);
        currGraph_->addDependencyGraph(graph);
        node_handle_t value = createFuncDataNode(graph, true, false);
        // The instance is made here, so a call of `f::<int>(...)` reaches the value, not the
        // declaration: the declaration's parameter defaults travel with the value.
        if (const auto found = defaults_.find(graph.get()); found != defaults_.end()) {
            defaultsOfValue_[value] = found->second;
        }
        return value;
    }
    if (nodeIsKind(target, runtime::GCNodeKind::Dref) &&
        std::holds_alternative<oper_group_ptr_t>(nodeGraphOf(target)->drefTarget(target))) {
        if (!given.empty()) {
            diags_->of(SemanticDiag::InvalidTypeArgument)
                .atOrigin(gct->origin())
                .commit("::<...>", "a built-in operator takes its type arguments by position");
            throw BuildAbortException();
        }
        explicitOperatorTypes_[target] = std::move(types);
        return target;
    }
    diags_->of(SemanticDiag::InvalidTypeArgument)
        .atOrigin(gct->origin())
        .commit("::<...>", "the callee does not declare type parameters");
    throw BuildAbortException();
}
} // namespace camel::compile::gir
