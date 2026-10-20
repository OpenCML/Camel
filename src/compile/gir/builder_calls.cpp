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
 * Calls: the argument list of a call, its partial application and the nodes a function value
 * is made of.
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

// Create a function-value node for a subgraph. When allowParameterization is
// true, parametrizeClosure() may convert the subgraph's closure captures into
// with parameters, which directly changes the subgraph's port structure. This
// only happens during initial compilation while Graph is still the mutable
// compile-time carrier.
// Create a function-producing node for `graph` in the current owner graph.
//
// This helper is the canonical point where compile-time graph references become IR-level
// dependencies. Every emitted FUNC node or static Function object must be backed by an explicit
// dependency edge; a self-recursive graph lists itself as a dependency.
//
// When `allowParameterization=true`, unresolved closure captures may be rewritten into explicit
// with-ports on the target graph. That mutation is only valid during the build phase.
node_handle_t Builder::valueNodeOf(const std::any &res) {
    if (res.type() == typeid(graph_ptr_t)) {
        return createFuncDataNode(any_cast<graph_ptr_t>(res), true, false);
    }
    ASSERT(res.type() == typeid(node_handle_t), "Expression did not produce a value node.");
    return any_cast<node_handle_t>(res);
}

node_handle_t Builder::elementNodeOf(const std::string &name, const GCT::node_ptr_t &literal) {
    node_handle_t node{};
    refOrigin_ = literal->origin();
    if (const std::string own = moduleLevelName(name); nodeAt(own).has_value()) {
        node = valueOf(resolveNodeByRef(own));
    } else {
        // Not a value that was bound: the name of a function (`[inc, dec]`), looked up as
        // the name is anywhere else. An unknown name is reported at the literal.
        const GCT::node_ptr_t dref = tree_->makeDRef(name);
        dref->setOrigin(literal->origin());
        node = visitDRefNode(dref);
    }
    return namedValueOf(node, name, literal);
}

node_handle_t
Builder::namedValueOf(node_handle_t node, const std::string &name, const GCT::node_ptr_t &origin) {
    node = functionValueOf(node, origin);
    rejectNameWithoutValue(node, name, origin);
    return node;
}

node_handle_t Builder::functionValueOf(node_handle_t node, const GCT::node_ptr_t &origin) {
    if (boundWithArgs_.contains(node)) {
        // `f<w>` as a value is a partial application (design principles 4.4). The binding
        // used to be dropped here, without a word.
        if (node_handle_t applied = partialApplicationOf(node, origin)) {
            return applied;
        }
        // Not one function for these arguments. A name may still be settled by the position
        // it stands in (a parameter of function type); a value cannot.
        if (!nodeIsKind(node, runtime::GCNodeKind::Dref)) {
            rejectUnappliedBinding(node, origin);
        }
        return node;
    }
    if (!nodeIsKind(node, runtime::GCNodeKind::Dref)) {
        return node;
    }
    // A function named as a value: a decorated function yields what its decorators return, a
    // plain one its function value.
    const auto &target = nodeGraphOf(node)->drefTarget(node);
    if (std::holds_alternative<graph_ptr_t>(target)) {
        return lowerDecoratedGraphValue(std::get<graph_ptr_t>(target), origin);
    }
    if (std::holds_alternative<graph_vec_ptr_t>(target)) {
        auto graphs = asCompileGraphVec(target);
        if (graphs && graphs->size() == 1) {
            return createFuncDataNode(graphs->front(), true, false);
        }
    }
    if (std::holds_alternative<oper_group_ptr_t>(target)) {
        // A library operator with one signature that does not depend on its arguments
        // (`trim: (s: string) => string`) is one function, like a user function of that
        // name: its value is a function that calls it.
        const auto &ops = std::get<oper_group_ptr_t>(target);
        if (ops->resolvers().size() == 1) {
            const auto &[uri, resolver] = ops->resolvers().front();
            if (FunctionType *fixed = resolver->fixedSignature()) {
                auto op = std::make_shared<OperatorIndex>(ops->name(), fixed, uri);
                if (compile_graph_ptr_t graph = operatorValueGraphOf(op, origin)) {
                    return createFuncDataNode(graph, true, false);
                }
            }
        }
    }
    return node;
}

node_handle_t Builder::createFuncDataNode(
    const graph_ptr_t &graph, bool callableAsResult, bool allowParameterization,
    node_handle_t branch) {
    ASSERT(
        !(callableAsResult && allowParameterization),
        "Cannot enable both callableAsResult and allowParameterization options.");
    ASSERT(currGraph_ != nullptr, "Current owner graph is null when creating a function node.");
    ASSERT(graph != nullptr, "Target graph is null when creating a function node.");
    // Its captures are known once its body is built: a function of the top level is built
    // here unless it is (or it is being built: a recursive use).
    ensureFunctionBuilt(graph);

    // Centralize dependency bookkeeping here so every FUNC node and static Function value obeys
    // the same graph-reference invariant. Self-dependency is intentional: a recursive graph lists
    // itself as a dependency.
    currGraph_->addDependencyGraph(graph);

    auto sourceContext = context_ ? context_->sourceContext() : nullptr;
    auto graphOrigin   = sourceContext ? sourceContext->debugMap().graphOrigin(graph->stableId())
                                       : camel::source::kInvalidOriginId;
    auto markMacroNode = [&](node_handle_t node) {
        if (node != nullptr && isMacroGraph(graph)) {
            setNodeMacro(node, true);
        }
    };
    const bool hasClosure = !graph->draft().closureNodes().empty();
    const bool valueMade  = valueUsedGraphs_.contains(graph.get());

    if (allowParameterization && (graph->parameterized() || !hasClosure || !valueMade)) {
        // A direct call: a FUNC node. At the first one the callee's capture ports become
        // with-ports after its declared ones (it is "parameterized"); every direct call then
        // passes its captures as With inputs after the declared `with` arguments, linked by
        // the link step (addCaptureSite, linkCaptureSites).
        if (!graph->parameterized()) {
            const std::vector<draft_node_ref_t> closureNodes(
                graph->draft().closureNodes().begin(),
                graph->draft().closureNodes().end());
            for (draft_node_ref_t closureId : closureNodes) {
                graph->draft().removeNormPort(closureId);
                graph->draft().appendWithPort(closureId);
                graph->draft().removeClosureNode(closureId);
            }
            graph->setParameterized(true);
        }
        node_handle_t funcNode = currGraph_->addFuncNode(graph, 0);
        markMacroNode(funcNode);
        addCaptureSite(funcNode, graph, branch);
        bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, funcNode);
        return funcNode;
    }

    if (callableAsResult && graph->parameterized() && graph->funcType()->hasExitType() &&
        !capturedValues_[graph.get()].empty()) {
        // A direct call has turned the captures of `graph` into with-parameters, which
        // each direct call passes. A function value has no place for them: called through
        // the value, the function read parameters nobody had set. The value is therefore
        // that of a function which captures the same values and passes them on.
        return createFuncDataNode(forwardingGraphOf(graph), true, false);
    }

    // A function value. It holds the captures the graph has now (filled into it), so the
    // graph can take no more (see captureInto).
    valueUsedGraphs_.insert(graph.get());
    auto *staticFunc     = createStaticFunction(currGraph_, graph);
    const auto slotIndex = currGraph_->addStaticSlot(
        camel::core::rtdata::toSlot<StaticFunction *>(staticFunc),
        graph->funcType());
    auto dataNode = currGraph_->draft().node(currGraph_->draft().addDataNode(
        graph->funcType(),
        slotIndex,
        static_cast<uint8_t>(runtime::kGCNodeFlagConstant)));
    markMacroNode(dataNode);
    node_handle_t resultNode = dataNode;
    if (hasClosure) {
        // The closure: the captured values, as the current graph sees them, in the order of
        // the closure ports.
        node_vec_t refNodes;
        const std::vector<CapturedValue> captured = capturedValues_[graph.get()];
        for (const CapturedValue &value : captured) {
            refNodes.push_back(captureInto(currGraph_, value.origin, value.name));
        }
        std::vector<size_t> closureSlots;
        closureSlots.reserve(refNodes.size());
        for (size_t i = 0; i < refNodes.size(); ++i) {
            closureSlots.push_back(i);
        }
        auto fillPayload =
            runtime::makeFillPayload(runtime::GCFillKind::FunctionClosure, closureSlots);
        auto fillNode = currGraph_->addFillNode(graph->funcType(), fillPayload);
        markMacroNode(fillNode);
        linkNodes(LinkType::Norm, dataNode, fillNode);
        for (const auto &refNode : refNodes) {
            linkNodes(LinkType::With, refNode, fillNode);
        }
        resultNode = fillNode;
    }
    if (!callableAsResult) {
        // A direct call of a graph whose value was made before: called through a value.
        auto callNode = currGraph_->addCallNode(graph->funcType()->exitType());
        markMacroNode(callNode);
        linkNodes(LinkType::With, resultNode, callNode);
        resultNode = callNode;
    }
    bindGraphScopedFuncNodeDebug(sourceContext, graphOrigin, graph, resultNode);
    return resultNode;
}

node_handle_t Builder::partialApplicationOf(
    node_handle_t target, const GCT::node_ptr_t &origin, FunctionType *expected) {
    const auto boundIt = boundWithArgs_.find(target);
    if (boundIt == boundWithArgs_.end() || genericDrefs_.contains(target)) {
        return nullptr;
    }
    std::vector<Argument> bound = boundIt->second;
    type_vec_t boundTypes;
    for (Argument &arg : bound) {
        arg.node = functionValueOf(arg.node, arg.expr);
        if (nodeIsKind(arg.node, runtime::GCNodeKind::Dref)) {
            return nullptr; // a name without a function value among the arguments
        }
        boundTypes.push_back(valueTypeOf(nodeTypeOf(arg.node)));
    }
    // The one function the callee stands for with these `with` arguments (and, when the
    // position says which function it wants, with the parameters that are left).
    const auto fits = [&](FunctionType *type) {
        if (type == nullptr || !type->hasExitType() || type->withTypesCount() < bound.size()) {
            return false;
        }
        for (size_t i = 0; i < bound.size(); ++i) {
            if (!entersImplicitly(type->withTypeAt(i), boundTypes[i])) {
                return false;
            }
        }
        if (expected == nullptr) {
            return true;
        }
        if (type->withTypesCount() - bound.size() != expected->withTypesCount() ||
            type->normTypesCount() != expected->normTypesCount()) {
            return false;
        }
        for (size_t i = 0; i < expected->withTypesCount(); ++i) {
            if (!type->withTypeAt(bound.size() + i)->equals(expected->withTypeAt(i))) {
                return false;
            }
        }
        for (size_t i = 0; i < expected->normTypesCount(); ++i) {
            if (!type->normTypeAt(i)->equals(expected->normTypeAt(i))) {
                return false;
            }
        }
        return true;
    };
    FunctionType *calleeType = nullptr;
    if (nodeIsKind(target, runtime::GCNodeKind::Dref)) {
        const auto &drefTarget = nodeGraphOf(target)->drefTarget(target);
        size_t candidates      = 0;
        if (std::holds_alternative<graph_vec_ptr_t>(drefTarget)) {
            if (auto graphs = asCompileGraphVec(drefTarget)) {
                for (const auto &g : *graphs) {
                    if (!decoratedOf_.contains(g.get()) && fits(g->funcType())) {
                        calleeType = g->funcType();
                        ++candidates;
                    }
                }
            }
        } else if (std::holds_alternative<oper_group_ptr_t>(drefTarget)) {
            // An operator whose signature does not depend on its arguments. One whose type
            // is computed from the argument types has no parameter types before it is
            // called, so its binding cannot stand alone.
            for (const auto &[uri, resolver] :
                 std::get<oper_group_ptr_t>(drefTarget)->resolvers()) {
                FunctionType *fixed = resolver->fixedSignature();
                if (fixed && fits(fixed)) {
                    calleeType = fixed;
                    ++candidates;
                }
            }
        }
        if (candidates != 1) {
            return nullptr;
        }
    } else if (
        Type *type = valueTypeOf(nodeTypeOf(target)); type != nullptr &&
                                                      type->code() == TypeCode::Function &&
                                                      fits(tt::as_ptr<FunctionType>(type))) {
        calleeType = tt::as_ptr<FunctionType>(type);
    } else {
        return nullptr;
    }
    for (size_t i = 0; i < calleeType->withTypesCount(); ++i) {
        if (calleeType->withIsVarAt(i)) {
            // DESIGN-QUESTION(partial-application-var): a `with` parameter declared `var`
            // is a variable the function writes. Binding one ahead of the call would make
            // the function value a writer of a captured variable; whether that is meant has
            // not been decided, so it is refused rather than guessed.
            diags_->of(SemanticDiag::ConstructNotSupported)
                .atOrigin(origin->origin())
                .commit(
                    "f<w> as a value",
                    "a partial application is implemented for functions without 'var' "
                    "context parameters; this one has '" +
                        calleeType->toString() + "'. Call it with its arguments in place");
            throw BuildAbortException();
        }
    }

    // The bound values and, unless it is named, the callee: bound to names no program can
    // write, in the scope the expression stands in, so that the new graph captures them the
    // way any closure captures a name. Nothing refers to a node of another graph.
    std::vector<std::string> boundNames;
    for (const Argument &arg : bound) {
        boundNames.push_back(bindTemporary(valueOf(arg.node)));
    }
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(origin->origin());
        return node;
    };
    const auto nameNode    = [&](const std::string &name) { return make(tree_->makeDRef(name)); };
    GCT::node_ptr_t callee = nullptr;
    if (nodeIsKind(target, runtime::GCNodeKind::Dref)) {
        const auto known = boundWithCallee_.find(target);
        ASSERT(known != boundWithCallee_.end(), "A bound callee has its expression recorded.");
        callee = tree_->clone(known->second);
    } else {
        callee = nameNode(bindTemporary(target));
    }
    boundWithArgs_.erase(target);
    boundWithCallee_.erase(target);

    // The function: the `with` parameters that are left, then the ordinary parameters.
    FunctionTypeFactory factory;
    factory.setModifiers(calleeType->modifiers());
    factory.setExitType(calleeType->exitType());
    const size_t withCount = calleeType->withTypesCount();
    const auto nameAt      = [&](size_t argIndex, const char *prefix, size_t i) {
        return argIndex < calleeType->argNamesCount() && !calleeType->argNameAt(argIndex).empty()
                   ? std::string(calleeType->argNameAt(argIndex))
                   : std::format("{}{}", prefix, i);
    };
    for (size_t i = bound.size(); i < withCount; ++i) {
        factory.addWithArg(nameAt(i, "__w", i), calleeType->withTypeAt(i), false);
    }
    for (size_t i = 0; i < calleeType->normTypesCount(); ++i) {
        factory.addNormArg(
            nameAt(withCount + i, "__n", i),
            calleeType->normTypeAt(i),
            calleeType->normIsVarAt(i));
    }
    FunctionType *type = factory.build();

    compile_graph_ptr_t graph     = enterScope(type);
    const bool outerSynced        = synced_;
    const node_handle_t outerLast = lastSyncedNode_;
    const auto restore            = [&] {
        synced_                  = outerSynced;
        lastSyncedNode_          = outerLast;
        bodyStates_[graph.get()] = BodyState::Built;
    };
    try {
        bodyStates_[graph.get()] = BodyState::Open;
        // Its body is the one call `f<bound..., rest...>(args...)`, built as any call is.
        GCT::node_ptr_t with = make(tree_->makeWith(withCount));
        *with << callee;
        for (const std::string &name : boundNames) {
            *with << nameNode(name);
        }
        size_t index = 0;
        for (draft_node_ref_t port : graph->draft().withPorts()) {
            const std::string name = "$with" + std::to_string(index++);
            nodeScope_->insert(name, graph->draft().node(port));
            *with << nameNode(name);
        }
        GCT::node_ptr_t call = make(tree_->makeLink(calleeType->normTypesCount()));
        *call << with;
        index = 0;
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            const std::string name = "$norm" + std::to_string(index++);
            nodeScope_->insert(name, graph->draft().node(port));
            *call << nameNode(name);
        }
        // The call is the value of the function, so it runs whenever the function is called.
        synced_             = true;
        lastSyncedNode_     = {};
        node_handle_t value = visitLinkNode(call);
        setGraphReturnFromResult(graph, nodeIdOf(value), nodeIdOf(value), nodeTypeOf(value));
        restore();
        unit_->builtBodies.push_back(graph);
    } catch (...) {
        restore();
        leaveScope();
        throw;
    }
    leaveScope();
    return createFuncDataNode(graph, true, false);
}

compile_graph_ptr_t Builder::forwardingGraphOf(const compile_graph_ptr_t &target) {
    FunctionType *targetType = target->funcType();
    // Its own type object (a graph completes its function type in place): the declared
    // parameters and result of the target, and no captures yet.
    FunctionTypeFactory factory;
    factory.setModifiers(targetType->modifiers());
    factory.setExitType(targetType->exitType());
    const auto nameAt = [&](size_t argIndex, const char *prefix, size_t i) {
        return argIndex < targetType->argNamesCount() ? std::string(targetType->argNameAt(argIndex))
                                                      : std::format("{}{}", prefix, i);
    };
    for (size_t i = 0; i < targetType->withTypesCount(); ++i) {
        factory.addWithArg(
            nameAt(i, "__w", i),
            targetType->withTypeAt(i),
            targetType->withIsVarAt(i));
    }
    for (size_t i = 0; i < targetType->normTypesCount(); ++i) {
        factory.addNormArg(
            nameAt(targetType->withTypesCount() + i, "__n", i),
            targetType->normTypeAt(i),
            targetType->normIsVarAt(i));
    }
    FunctionType *type = factory.build();

    compile_graph_ptr_t graph = enterScope(type);
    try {
        auto sourceContext = context_ ? context_->sourceContext() : nullptr;
        auto targetOrigin  = sourceContext
                                 ? sourceContext->debugMap().graphOrigin(target->stableId())
                                 : camel::source::kInvalidOriginId;
        if (sourceContext && targetOrigin != camel::source::kInvalidOriginId) {
            sourceContext->debugMap().registerGraphOrigin(graph->stableId(), targetOrigin);
        }
        bodyStates_[graph.get()] = BodyState::Open;
        graph->addDependencyGraph(target);
        // The call the target's direct callers make: the arguments, then the target's
        // captures, which become captures of this graph (passed by the link step).
        node_handle_t call = graph->addFuncNode(target, 0);
        if (isMacroGraph(target)) {
            setNodeMacro(call, true);
        }
        for (draft_node_ref_t port : graph->draft().withPorts()) {
            linkNodes(LinkType::With, graph->draft().node(port), call);
        }
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            linkNodes(LinkType::Norm, graph->draft().node(port), call);
        }
        addCaptureSite(call, target);
        bindGraphScopedFuncNodeDebug(sourceContext, targetOrigin, target, call);
        // The summaries see the target as a function this graph calls: what the target
        // writes of the captured variables, this graph writes.
        (void)capturedUsesOf(target);
        setGraphReturnFromResult(graph, nodeIdOf(call), nodeIdOf(call), targetType->exitType());
        bodyStates_[graph.get()] = BodyState::Built;
        unit_->builtBodies.push_back(graph);
    } catch (...) {
        bodyStates_[graph.get()] = BodyState::Built;
        leaveScope();
        throw;
    }
    leaveScope();
    return graph;
}

node_handle_t Builder::argumentNodeOf(const std::any &res, const GCT::node_ptr_t &origin) {
    if (res.type() == typeid(graph_ptr_t)) {
        return createFuncDataNode(any_cast<graph_ptr_t>(res), true, false);
    }
    ASSERT(res.type() == typeid(node_handle_t), "Argument did not produce a value node.");
    return functionValueOf(any_cast<node_handle_t>(res), origin);
}

StaticArg Builder::staticArgOf(node_handle_t node) const {
    StaticArg arg(staticValueOf(node));
    node = throughCopy(node);
    if (!arg.value) {
        if (auto it = typeFoldedValues_.find(node); it != typeFoldedValues_.end()) {
            arg.value = it->second;
        }
    }
    if (auto it = arrayElementStatics_.find(node); it != arrayElementStatics_.end()) {
        arg.elements = it->second;
    }
    return arg;
}

// The part of a "no matching function" message for a call that wrote every argument in the
// parentheses although `name` takes all but the first `normCount` in angle brackets: the
// standard library's convention is that the value worked on is an ordinary argument and what
// configures the operation a with-argument (design principles 11.4). Names the misplaced
// arguments and gives the call as it should be written.
std::string Builder::withArgumentsHint(
    const std::string &name, const std::vector<Argument> &arguments, size_t normCount) const {
    std::vector<std::string> texts;
    texts.reserve(arguments.size());
    for (size_t i = 0; i < arguments.size(); ++i) {
        std::string text = sourceTextOf(arguments[i].expr);
        texts.push_back(text.empty() ? std::format("arg{}", i + 1) : std::move(text));
    }
    const auto joined = [&](size_t from, size_t to) {
        std::string out;
        for (size_t i = from; i < to; ++i) {
            out += (i == from ? "" : ", ") + texts[i];
        }
        return out;
    };
    const std::string withs = joined(normCount, texts.size());
    const std::string norms = joined(0, normCount);
    std::string hint        = std::format(
        "; '{}' takes {} in angle brackets, not in the parentheses: write {}<{}>({})",
        name,
        "'" + withs + "'",
        name,
        withs,
        norms);
    if (normCount == 1) {
        hint += std::format(" or {}->{}<{}>", norms, name, withs);
    }
    return hint;
}

node_handle_t Builder::visitOperand(const GCT::node_ptr_t &expr, const GCT::node_ptr_t &call) {
    if (expr->type() == GCT::NodeKind::DREF) {
        const std::string name = moduleLevelName(expr->name());
        refOrigin_             = expr->origin();
        if (nodeAt(name).has_value()) {
            if (node_handle_t node = resolveNodeByRef(name); isVariable(node)) {
                if (recorder_) {
                    recorder_->valueReferenced(
                        expr->origin(),
                        name,
                        declarationOf(name),
                        valueTypeOf(nodeTypeOf(node)));
                }
                return node;
            }
        }
    }
    return argumentNodeOf(visit(expr), call);
}

void Builder::requireMutableArgument(const Argument &arg, const std::string &callee) {
    if (auto name = immutableNameOf(arg.expr)) {
        diags_->of(SemanticDiag::ImmutablePassedToVarParam)
            .atOrigin(arg.expr->origin())
            .commit(*name, callee);
        throw BuildAbortException();
    }
}

} // namespace camel::compile::gir
