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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Decorators and tags: the attributes a decorator grants to a function and the graph a
 * decorated block becomes.
 */

#include "builder.h"
#include "builder_support.h"
#include "builder_helpers.h"

#include "camel/compile/gir/static_function.h"
#include "camel/core/numeric_convert.h"
#include "camel/core/operator_names.h"
#include "camel/core/rtdata/string.h"
#include "camel/execute/entry_args.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph.h"
#include "camel/utils/log.h"
#include "camel/utils/scope.h"
#include "camel/utils/str.h"
#include "camel/utils/type.h"
#include "core/module/entity_internal.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/other.h"

#include <atomic>
#include <cctype>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;

namespace camel::compile::gir {

node_handle_t
Builder::applyDecoratorAnno(const GCT::node_ptr_t &annoNode, node_handle_t funcValueNode) {
    ASSERT(annoNode && annoNode->type() == GCT::NodeKind::ANNO, "Expected ANNO node.");
    ASSERT(annoNode->size() == 1, "ANNO node should have exactly one lowered expression child.");
    ASSERT(funcValueNode != nullptr, "Decorator input function value is null.");

    // `@name` or `@name<args>`: either way the expression is the decorator to call.
    const auto &decorator = annoNode->at(0);

    const std::string tmpScopeName = "__decorator_tmp_scope_" + std::to_string(syntheticRefIndex_);
    const std::string tmpRefName   = "__decorator_tmp_ref_" + std::to_string(syntheticRefIndex_++);
    nodeScope_                     = nodeScope_->enter(tmpScopeName);
    nodeScope_->insert(tmpRefName, funcValueNode);

    GCT::node_ptr_t linkNode = tree_->makeLink(1);
    *linkNode << tree_->clone(decorator);
    *linkNode << tree_->makeDRef(tmpRefName);
    // A call of the decorator is reported at the decorator.
    linkNode->setOrigin(decorator->origin());
    linkNode->at(1)->setOrigin(decorator->origin());

    node_handle_t decoratedValue = visitLinkNode(linkNode);
    nodeScope_                   = nodeScope_->leave();
    return decoratedValue;
}

compile_graph_ptr_t Builder::buildDecoratedGraph(
    const std::string &funcName, const compile_graph_ptr_t &rawGraph,
    const std::vector<GCT::node_ptr_t> &annoNodes) {
    ASSERT(rawGraph != nullptr, "Raw function graph is null.");
    if (annoNodes.empty()) {
        return rawGraph;
    }

    // Decorators run at macro time: the graph applying them is a macro that yields the decorated
    // function once its inputs are static.
    // The factory is a separate graph, not a statement in the enclosing sync chain.
    // Otherwise applying a local decorator overwrites lastSyncedNode_ with a node
    // belonging to this factory, and the next enclosing call links across graphs.
    struct RestoreSync {
        bool &synced;
        node_handle_t &last;
        bool outerSynced;
        node_handle_t outerLast;
        ~RestoreSync() {
            synced = outerSynced;
            last   = outerLast;
        }
    } restoreSync{synced_, lastSyncedNode_, synced_, lastSyncedNode_};
    synced_                    = false;
    lastSyncedNode_            = {};
    graph_ptr_t decoratedGraph = enterScope(
        FunctionType::create(param_vec_t{}, param_vec_t{}, nullptr, Modifier::Macro),
        "__decorated__" + funcName + "_" + std::to_string(syntheticRefIndex_++));
    registerGraphOrigin(
        context_,
        decoratedGraph,
        annoNodes.back(),
        "gir.func.decorated.graph",
        {},
        {},
        true);

    // Seed value is the original function object.
    node_handle_t decoratedValue = createFuncDataNode(rawGraph, true, false);
    for (auto it = annoNodes.rbegin(); it != annoNodes.rend(); ++it) {
        decoratedValue = applyDecoratorAnno(*it, decoratedValue);
    }

    if (nodeTypeOf(decoratedValue)->code() != TypeCode::Function) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(annoNodes.front()->origin())
            .commit("decorator must return function", nodeTypeOf(decoratedValue)->toString());
        throw BuildAbortException();
    }

    setGraphReturnFromResult(
        decoratedGraph,
        nodeIdOf(decoratedValue),
        nodeIdOf(decoratedValue),
        nodeTypeOf(decoratedValue));
    leaveScope();
    return decoratedGraph;
}

// ---- Decorators that grant attributes, decorated blocks (language reference 7.6) ------------

std::optional<camel::core::attr::AttrSetId> Builder::staticGrantOf(const GCT::node_ptr_t &anno) {
    if (auto it = staticGrants_.find(anno); it != staticGrants_.end()) {
        return it->second;
    }
    // Asked first where the decorator is written (visitFuncNode before it enters the
    // function, registerGenericTemplates): the names of the decorator and of its arguments are
    // resolved there.
    const auto compute = [&]() -> std::optional<camel::core::attr::AttrSetId> {
        const GCT::node_ptr_t expr = anno->at(0);
        GCT::node_ptr_t callee     = expr;
        std::vector<GCT::node_ptr_t> args;
        if (expr->type() == GCT::NodeKind::WITH) {
            callee = expr->at(0);
            for (size_t i = 1; i < expr->size(); ++i) {
                args.push_back(expr->at(i));
            }
        }
        // A name the program binds itself (a value, a function, a generic function) is not
        // the operator: the decorator is applied as any other.
        const auto programName = [&](const std::string &written) {
            const std::string name = moduleLevelName(written);
            return nodeAt(name).has_value() || templateScope_->get(name).has_value() ||
                   graphsAt(name).has_value() || decoratedGraphAt(name).has_value() ||
                   (unit_ != nullptr && unit_->values.contains(name));
        };
        // The entity an imported name stands for (design principles 10.2, layers 3 to 5).
        const auto imported =
            [&](const GCT::node_ptr_t &ref) -> std::optional<camel::core::module::entity> {
            const std::string written = ref->name();
            if (programName(written) || isOperatorName(written)) {
                return std::nullopt;
            }
            if (module_->hasImportedRef(written, ImportLayer::Named)) {
                return module_->getImportedEntity(written, ImportLayer::Named);
            }
            if (auto match = throughUsing(written, ref)) {
                return match->ownName.empty() ? match->entity : std::nullopt;
            }
            if (module_->hasImportedRef(written, ImportLayer::Ambient)) {
                return module_->getImportedEntity(written, ImportLayer::Ambient);
            }
            return std::nullopt;
        };
        if (callee->type() != GCT::NodeKind::DREF) {
            return std::nullopt;
        }
        const auto entity = imported(callee);
        if (!entity.has_value() || !entity->isOperGroup() ||
            entity->operGroup()->resolvers().size() != 1) {
            return std::nullopt;
        }
        const auto grant = camel::core::OperatorTable::instance().grantOf(
            entity->operGroup()->resolvers().front().first);
        if (grant == nullptr) {
            return std::nullopt;
        }
        // The with-arguments must be constants: literals, or constants a module exports.
        std::vector<data_ptr_t> values;
        for (const GCT::node_ptr_t &arg : args) {
            if (arg->type() == GCT::NodeKind::DATA) {
                values.push_back(arg->data());
                continue;
            }
            if (arg->type() != GCT::NodeKind::DREF) {
                return std::nullopt;
            }
            const auto value = imported(arg);
            if (!value.has_value() || !value->isValue()) {
                return std::nullopt;
            }
            values.push_back(value->value());
        }
        const std::optional<camel::core::attr::AttrValueId> granted =
            grant->value ? grant->value(values)
                         : std::optional<camel::core::attr::AttrValueId>(camel::core::attr::unit());
        if (!granted.has_value()) {
            return std::nullopt;
        }
        return camel::core::attr::single(camel::core::attr::key(grant->key), *granted);
    };
    std::optional<camel::core::attr::AttrSetId> result;
    try {
        result = compute();
    } catch (const BuildAbortException &) {
        result = std::nullopt; // reported, if at all, when the decorator is applied
    }
    staticGrants_.emplace(anno, result);
    return result;
}

std::pair<camel::core::attr::AttrSetId, size_t>
Builder::staticGrantsOf(const GCT::node_ptr_t &func) {
    std::vector<GCT::node_ptr_t> annos;
    for (size_t i = 2; i < func->size(); ++i) {
        if (func->at(i) && func->at(i)->type() == GCT::NodeKind::ANNO) {
            annos.push_back(func->at(i));
        }
    }
    camel::core::attr::AttrSetId granted = camel::core::attr::kNoAttrs;
    size_t count                         = 0;
    // The decorator written last applies first: a run of granting ones from there is applied
    // to the function as written, before any other decorator sees it.
    for (auto it = annos.rbegin(); it != annos.rend(); ++it) {
        const auto grant = staticGrantOf(*it);
        if (!grant.has_value()) {
            break;
        }
        granted = camel::core::attr::unite(granted, *grant);
        ++count;
    }
    return {granted, count};
}

std::vector<GCT::node_ptr_t> Builder::appliedDecoratorsOf(const GCT::node_ptr_t &func) {
    std::vector<GCT::node_ptr_t> annos;
    for (size_t i = 2; i < func->size(); ++i) {
        if (func->at(i) && func->at(i)->type() == GCT::NodeKind::ANNO) {
            annos.push_back(func->at(i));
        }
    }
    annos.resize(annos.size() - staticGrantsOf(func).second);
    return annos;
}

node_handle_t Builder::visitDecoratedBlock(const GCT::node_ptr_t &link, bool unused) {
    ENTER("DECORATED_BLOCK");
    // `@d { ... }` is the block as a function of no parameters, passed to the decorators and
    // called right where it is written (language reference 7.6). The block is lowered as a
    // plain block would be (its scope, its exits, its value); the function's value is the
    // block's, and is dropped when nothing takes the block's value.
    const GCT::node_ptr_t func = link->at(0);
    const GCT::node_ptr_t body = func->at(1);
    staticGrantsOf(func); // resolved here, where the decorators are written
    const std::vector<GCT::node_ptr_t> decorators = appliedDecoratorsOf(func);
    if (decorators.empty()) {
        // Every decorator grants attributes as the program is built: what they make is the
        // block's own function with the attributes, called here, which is the block itself
        // with the attributes. Built in place, as the same block without decorators is.
        AttrScope attrs(
            currentAttrs_,
            camel::core::attr::unite(currentAttrs_, staticGrantsOf(func).first));
        unusedStatement_     = unused ? body : nullptr;
        node_handle_t result = visitExecNode(body);
        LEAVE("DECORATED_BLOCK");
        return result;
    }
    if (unused) {
        droppedBodies_.insert(body);
    }
    compile_graph_ptr_t graph = visitFuncNode(func);
    blockGraphs_.insert(graph.get());
    node_handle_t value = createFuncDataNode(graph, true, false);
    for (auto it = decorators.rbegin(); it != decorators.rend(); ++it) {
        value = applyDecoratorAnno(*it, value);
    }
    // The call, as if written here: `(decorated)()`.
    const std::string scopeName = "__block_tmp_scope_" + std::to_string(syntheticRefIndex_);
    const std::string refName   = "__block_tmp_ref_" + std::to_string(syntheticRefIndex_++);
    nodeScope_                  = nodeScope_->enter(scopeName);
    nodeScope_->insert(refName, value);
    GCT::node_ptr_t call = tree_->makeLink(0);
    GCT::node_ptr_t ref  = tree_->makeDRef(refName);
    ref->setOrigin(link->origin());
    call->setOrigin(link->origin());
    *call << ref;
    node_handle_t result{};
    try {
        result = visitLinkNode(call);
    } catch (...) {
        nodeScope_ = nodeScope_->leave();
        throw;
    }
    nodeScope_ = nodeScope_->leave();
    LEAVE("DECORATED_BLOCK");
    return result;
}

graph_set_ptr_t
Builder::overloadsOfDecorated(const std::string &name, const compile_graph_ptr_t &decorated) {
    auto graphs = graphsAt(name);
    if (!graphs.has_value() || graphs.value()->size() < 2) {
        return nullptr;
    }
    for (const auto &g : *graphs.value()) {
        auto it = decoratedOf_.find(g.get());
        if (it != decoratedOf_.end() && it->second == decorated) {
            return graphs.value();
        }
    }
    return nullptr;
}

node_handle_t Builder::lowerDecoratedGraphValue(
    const compile_graph_ptr_t &decoratedGraph, const GCT::node_ptr_t &origin) {
    currGraph_->addDependencyGraph(decoratedGraph);
    if (!decoratedGraph->funcType()->hasExitType() ||
        decoratedGraph->funcType()->exitType()->code() != TypeCode::Function) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(origin->origin())
            .commit(
                "decorated graph should return function",
                decoratedGraph->funcType()->toString());
        throw BuildAbortException();
    }
    node_handle_t decoratorFactoryValue = createFuncDataNode(decoratedGraph, true, false);
    node_handle_t decoratorInvoke = currGraph_->addCallNode(decoratedGraph->funcType()->exitType());
    linkNodes(LinkType::With, decoratorFactoryValue, decoratorInvoke);
    return decoratorInvoke;
}

node_handle_t
Builder::applyTagOperator(const char *op, const std::string &name, const GCT::node_ptr_t &origin) {
    // An ordinary call, built from a synthesized GCT so that capturing the operand from an
    // enclosing graph, overload resolution and origins are what they are for any call.
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(origin->origin());
        return node;
    };
    GCT::node_ptr_t call = make(tree_->makeLink(1));
    *call << make(tree_->makeDRef(op)) << make(tree_->makeDRef(name));
    return visitLinkNode(call);
}

} // namespace camel::compile::gir
