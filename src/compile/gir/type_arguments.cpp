// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Explicit types are consumed while building concrete graphs, never passed as values.

#include "builder.h"
#include "builder_helpers.h"
#include "builder_support.h"
#include "camel/core/data/primary.h"
#include "camel/utils/str.h"
#include <algorithm>
#include <unordered_set>
#include <utility>

namespace camel::compile::gir {
using namespace core::type;
using namespace core::error;

FunctionType *Builder::signatureForArgumentContext(node_handle_t target, size_t withWritten) const {
    if (const auto generic = genericDrefs_.find(target); generic != genericDrefs_.end()) {
        // A name with several generic functions gives the arguments a type only when every
        // overload declares the same parameters; a set whose entries differ says nothing about
        // which one a call means, exactly as an operator set whose entries differ does.
        std::vector<generic_template_ptr_t> candidates = genericOverloadsOf(target);
        if (withWritten > 0) {
            std::erase_if(candidates, [&](const generic_template_ptr_t &candidate) {
                return candidate->funcType->withTypesCount() < withWritten;
            });
        }
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
            // A rule the module declares gives its parameters, a generic declaration too; a rule
            // whose signature is computed has none of its own (design principles 6.4: the
            // declaration is the one source of a signature).
            FunctionType *fixed = resolver ? resolver->fixedSignature() : nullptr;
            if (fixed == nullptr && resolver) {
                fixed = resolver->declaredSignature();
            }
            if (fixed == nullptr && resolver) {
                fixed = resolver->graftedSignature();
            }
            // A rest parameter takes arguments of any types and is expanded at the call: it is
            // not a position an argument is held to a type at.
            if (fixed != nullptr && (fixed->hasRest() || fixed->hasWithRest())) {
                fixed = nullptr;
            }
            if (fixed == nullptr || fixed->withTypesCount() < withWritten) {
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
    // The `with` arguments written after these types and the names they carry (visitWithNode); the
    // outer application's are not this one's.
    const size_t withWritten = std::exchange(typeApplicationWithWritten_, 0);
    const std::vector<std::optional<std::string>> withNames =
        std::exchange(typeApplicationWithNames_, {});
    const std::vector<Type *> withWrittenTypes = std::exchange(typeApplicationWithTypes_, {});
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
                // The `with` parameters tell overloads of one name apart as the ordinary ones do:
                // the arguments written fit when they are not more than the parameters, not fewer
                // than the ones without a default, and every name written is a parameter's.
                {
                    const FunctionType *signature = candidate->funcType;
                    const size_t declared         = signature->withTypesCount();
                    size_t required               = declared;
                    if (const auto defaults = defaults_.find(candidate.get());
                        defaults != defaults_.end()) {
                        required = 0;
                        for (const auto &value : defaults->second.with) {
                            required += value.has_value() ? 0 : 1;
                        }
                    }
                    if (withWritten > declared || withWritten < required) {
                        continue;
                    }
                    bool namesFit = true;
                    for (const auto &written : withNames) {
                        if (!written.has_value()) {
                            continue;
                        }
                        bool found = false;
                        for (size_t i = 0; signature->hasArgNames() && i < declared; ++i) {
                            found = found || signature->argNameAt(i) == *written;
                        }
                        namesFit = namesFit && found;
                    }
                    if (!namesFit) {
                        continue;
                    }
                    // And so do their types, where the argument's type is known: a parameter
                    // that mentions no type variable must take it (a number enters a number).
                    bool typesFit = true;
                    for (size_t w = 0; w < withWrittenTypes.size() && typesFit; ++w) {
                        Type *argument = withWrittenTypes[w];
                        if (argument == nullptr) {
                            continue;
                        }
                        size_t slot = w;
                        if (w < withNames.size() && withNames[w].has_value()) {
                            for (size_t p = 0; signature->hasArgNames() && p < declared; ++p) {
                                if (signature->argNameAt(p) == *withNames[w]) {
                                    slot = p;
                                }
                            }
                        }
                        if (slot >= declared) {
                            continue;
                        }
                        Type *parameter = signature->withTypeAt(slot);
                        if (containsTypeVar(parameter)) {
                            continue;
                        }
                        Type *given = valueTypeOf(argument);
                        typesFit = parameter->assignableFrom(given) ||
                                   (isNumericScalar(parameter->code()) &&
                                    isNumericScalar(given->code()));
                    }
                    if (!typesFit) {
                        continue;
                    }
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
        if (graph == nullptr) {
            // Only a scope that holds an attempt back answers "no instance" instead of aborting,
            // and what the body said about the types it refused is that scope's to keep
            // (`SpeculativeInstantiation`). This position has no function then either, so the
            // attempt fails the same way a call that fails inside it does: the caller above the
            // scope is the one that decides what it means -- a search that has a candidate left,
            // or a rule's `when` template, for which a type it cannot be written for is not a
            // reason to report anything where the type was tried (the rule on
            // `generic-rule-instantiation-set-and-predicate`, §3).
            throw BuildAbortException();
        }
        currGraph_->addDependencyGraph(graph);
        // An instance of an `#impl` function that has an executor, called directly, is a call of
        // its kernel (design principles 10.4); the type arguments the call wrote are the kernel's
        // own when its variable is stated by them alone. Anywhere but the callee position the
        // instance is the function value, which is the body.
        if (outerAllowed && !insideImplBody()) {
            if (std::optional<ImplKernel> kernel = implKernelOfGraph(graph); kernel.has_value()) {
                node_handle_t drefNode{currGraph_->addDrefNode(
                    std::variant<graph_vec_ptr_t, oper_group_ptr_t, graph_ptr_t>{kernel->group})};
                registerNodeOrigin(context_, drefNode, gct, "gir.dref.impl");
                if (!kernel->types.empty()) {
                    explicitOperatorTypes_[drefNode] = kernel->types;
                }
                return drefNode;
            }
        }
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
