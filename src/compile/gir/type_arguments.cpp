// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Explicit types are consumed while building concrete graphs, never passed as values.

#include "builder.h"
#include "builder_support.h"
#include "camel/core/data/primary.h"
#include "camel/utils/str.h"
#include <algorithm>
#include <utility>

namespace camel::compile::gir {
using namespace core::type;
using namespace core::error;

FunctionType *Builder::signatureForArgumentContext(node_handle_t target) const {
    if (const auto generic = genericDrefs_.find(target); generic != genericDrefs_.end()) {
        return generic->second->funcType;
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
        if (ops->resolvers().size() == 1) {
            if (FunctionType *fixed = ops->resolvers()[0].second->fixedSignature()) {
                return fixed;
            }
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
        const auto tmpl = found->second;
        std::vector<std::string> names;
        collectTypeVars(tmpl->funcType, names);
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
                evaluateWhenCondition(when->condition, tmpl->funcType, bindings, {});
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
        return createFuncDataNode(graph, true, false);
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
