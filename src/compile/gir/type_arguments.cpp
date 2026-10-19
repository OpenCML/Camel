/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Explicit types are consumed while building concrete graphs, never passed as values.
#include "builder.h"
#include "builder_support.h"
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
            if (const auto fixed =
                    dynamic_cast<StaticFuncTypeResolver *>(ops->resolvers()[0].second.get())) {
                return fixed->funcType();
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
    type_vec_t types;
    for (size_t i = 1; i < gct->size(); ++i) {
        types.push_back(boundType(gct->at(i)->dataType()));
    }
    if (const auto found = genericDrefs_.find(target); found != genericDrefs_.end()) {
        const auto tmpl = found->second;
        std::vector<std::string> names;
        collectTypeVars(tmpl->funcType, names);
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
        auto graph = instantiateGeneric(tmpl, bindings, gct);
        currGraph_->addDependencyGraph(graph);
        return createFuncDataNode(graph, true, false);
    }
    if (nodeIsKind(target, runtime::GCNodeKind::Dref) &&
        std::holds_alternative<oper_group_ptr_t>(nodeGraphOf(target)->drefTarget(target))) {
        explicitOperatorTypes_[target] = std::move(types);
        return target;
    }
    diags_->of(SemanticDiag::InvalidTypeArgument)
        .atOrigin(gct->origin())
        .commit("::<...>", "the callee does not declare type parameters");
    throw BuildAbortException();
}
} // namespace camel::compile::gir
