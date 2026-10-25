// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

#include <algorithm>
#include <atomic>
#include <cctype>
#define DEBUG_LEVEL -1

using namespace std;
using namespace camel::core::error;
using namespace camel::core::context;
using namespace camel::core::module;
using namespace camel::core::data;
using namespace camel::core::type;
namespace sem = camel::core::sem;

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
    if (res.type() == typeid(StaticTuple)) {
        // A build-time tuple used where a runtime value is needed. A comprehension becomes one
        // FILL here, at its first such use; a rest parameter's `ts` is not a value at all.
        const StaticTuple &tuple = any_cast<const StaticTuple &>(res);
        if (!tuple.materializable) {
            const GCT::node_ptr_t at = tuple.origin;
            diags_->of(SemanticDiag::StaticTupleEscapes)
                .atOrigin(at != nullptr ? at->origin() : 0)
                .commit(tuple.name.empty() ? std::string("a build-time tuple") : tuple.name);
            throw BuildAbortException();
        }
        return materializeStaticTuple(tuple, tuple.origin);
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
        node = valueNodeOf(visitDRefNode(dref));
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
        // used to be dropped here, without a word. It is located at `f<w>` itself, not at the
        // statement the binding is written in (1.6).
        const auto withIt = boundWithOrigin_.find(node);
        const GCT::node_ptr_t at =
            withIt != boundWithOrigin_.end() ? withIt->second : origin;
        if (node_handle_t applied = partialApplicationOf(node, at)) {
            return applied;
        }
        // Not one function for these arguments. A name may still be settled by the position
        // it stands in (a parameter of function type); a value cannot.
        if (!nodeIsKind(node, runtime::GCNodeKind::Dref)) {
            rejectUnappliedBinding(node, at);
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
    const graph_ptr_t &graph, bool callableAsResult, bool allowParameterization) {
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
    auto graphOrigin   = sourceContext ? sourceContext->debugMap().graphOrigin(graph->graphId())
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
        addCaptureSite(funcNode, graph);
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
    auto dataNode = currGraph_->draft().ref(currGraph_->draft().addDataNode(
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
    // Where the whole binding is: the `f<w>` expression (boundWithOrigin_), otherwise the position
    // the caller gives (a value passed where a function of a type is expected). The expression is
    // the union of the node's own span (`<w>`) and its operands, as for a call (design
    // principles 5.1): the callee and the angled arguments together.
    const auto originIt = boundWithOrigin_.find(target);
    const GCT::node_ptr_t where = originIt != boundWithOrigin_.end() ? originIt->second : origin;
    const auto exprSpan         = callExpressionSpan(context_, where);
    const auto exprOrigin       = deriveGirOrigin(
        context_,
        where,
        camel::source::OriginKind::GirNode,
        "gir.with.expr",
        {},
        exprSpan);
    const camel::source::origin_id_t at =
        exprOrigin != camel::source::kInvalidOriginId ? exprOrigin : where->origin();
    std::vector<Argument> bound = boundIt->second;
    for (const Argument &arg : bound) {
        if (arg.name) {
            // A function value has no parameter names to bind a name to, and the binding it
            // would make is not the prefix of parameters a value carries: name a parameter
            // only in a call (design principles 4.4).
            diags_->of(SemanticDiag::WithArgumentInValue)
                .atOrigin(arg.expr != nullptr ? arg.expr->origin() : at)
                .commit(*arg.name);
            throw BuildAbortException();
        }
    }
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
    //
    // A context argument chooses its overload exactly as an ordinary call does (design
    // principles 4.4, 5.1; d): a function whose `with` parameters take the bound values
    // exactly is tried first, and only when no function takes them exactly may a literal
    // enter a parameter type. With `with <k: int>` and `with <k: f32>` both declared,
    // `scale<2>` is the `int` one; `half<2>` with only `with <k: f32>` still binds the
    // literal as an f32. A name never converts, so a name no parameter takes exactly leaves
    // the binding refused.
    const auto fits = [&](FunctionType *type, bool entering) {
        if (type == nullptr || !type->hasExitType() || type->withTypesCount() < bound.size()) {
            return false;
        }
        for (size_t i = 0; i < bound.size(); ++i) {
            Type *param = type->withTypeAt(i);
            if (param == nullptr) {
                return false;
            }
            // Exactly: the bound value already has the parameter's type. This is the first
            // pass an ordinary call makes, with the argument types as they are, before any
            // literal takes a context type (enteringTypes, builder_operators.cpp).
            if (param->assignableFrom(boundTypes[i])) {
                continue;
            }
            if (!entering) {
                return false;
            }
            // A `with` parameter is a position with an expected type, like an ordinary
            // parameter (design principles 4.4, 5.1): `scale<2>` with `with <k: f32>` and
            // `at<[7.0, 8.0]>` with `with <xs: f32[]>` bind, and the values are converted
            // below.
            if (entersImplicitly(param, boundTypes[i])) {
                continue;
            }
            if (const std::optional<literal::Value> value = literalValueOfExpr(bound[i].expr);
                value.has_value() && literal::kindEnters(*value, param)) {
                continue;
            }
            // An element the type cannot hold still enters here: the conversion reports it.
            if (compositeLiteralEntersIn(bound[i].node, param, bound[i].expr)) {
                continue;
            }
            return false;
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
    // The functions the name stands for, in declaration order.
    std::vector<FunctionType *> candidates;
    if (nodeIsKind(target, runtime::GCNodeKind::Dref)) {
        const auto &drefTarget = nodeGraphOf(target)->drefTarget(target);
        if (std::holds_alternative<graph_vec_ptr_t>(drefTarget)) {
            if (auto graphs = asCompileGraphVec(drefTarget)) {
                for (const auto &g : *graphs) {
                    if (!decoratedOf_.contains(g.get())) {
                        candidates.push_back(g->funcType());
                    }
                }
            }
        } else if (std::holds_alternative<oper_group_ptr_t>(drefTarget)) {
            // An operator whose signature does not depend on its arguments. One whose type
            // is computed from the argument types has no parameter types before it is
            // called, so its binding cannot stand alone.
            for (const auto &[uri, resolver] :
                 std::get<oper_group_ptr_t>(drefTarget)->resolvers()) {
                if (FunctionType *fixed = resolver->fixedSignature()) {
                    candidates.push_back(fixed);
                }
            }
        }
    } else if (
        Type *type = valueTypeOf(nodeTypeOf(target));
        type != nullptr && type->code() == TypeCode::Function) {
        candidates.push_back(tt::as_ptr<FunctionType>(type));
    }
    // Exact matches first; a literal enters a parameter type only when there is none. Several
    // candidates in the pass that is used are an ambiguity (design principles 5.1, e): it is
    // recorded here and reported when the binding is refused for good, because the position
    // may still say which function is meant (a parameter of function type, overloadValueFor,
    // or the type of the call this value is passed to). Declaration order never chooses, and
    // `expected` settles the choice before either pass.
    ambiguousBindings_.erase(target);
    FunctionType *calleeType = nullptr;
    for (const bool entering : {false, true}) {
        std::vector<FunctionType *> fitted;
        for (FunctionType *candidate : candidates) {
            if (fits(candidate, entering)) {
                fitted.push_back(candidate);
            }
        }
        if (fitted.empty()) {
            continue;
        }
        if (fitted.size() > 1) {
            ambiguousBindings_[target] = std::move(fitted);
            return nullptr;
        }
        calleeType = fitted.front();
        break;
    }
    if (calleeType == nullptr) {
        return nullptr;
    }
    for (size_t i = 0; i < calleeType->withTypesCount(); ++i) {
        if (calleeType->withIsVarAt(i)) {
            // DESIGN-QUESTION(partial-application-var): a `with` parameter declared `var`
            // is a variable the function writes. Binding one ahead of the call would make
            // the function value a writer of a captured variable; whether that is meant has
            // not been decided, so it is refused rather than guessed.
            diags_->of(SemanticDiag::ConstructNotSupported)
                .atOrigin(at)
                .commit(
                    "f<w> as a value",
                    "a partial application is implemented for functions without 'var' "
                    "context parameters; this one has '" +
                        calleeType->toString() + "'. Call it with its arguments in place");
            throw BuildAbortException();
        }
    }

    // A literal bound as a context argument takes the type of its `with` parameter here, now
    // that the callee is known, exactly as a literal argument does at a call (design principles
    // 5.1): the captured value is of the parameter's type, so the call the partial application
    // makes receives it as it is. A value the type cannot hold is reported by coerceTo — for a
    // composite literal, at the element. A name still does not convert.
    for (size_t i = 0; i < bound.size(); ++i) {
        Type *param = calleeType->withTypeAt(i);
        if (param == nullptr || entersImplicitly(param, boundTypes[i])) {
            continue;
        }
        node_handle_t entered = checkValue(bound[i].node, param, bound[i].expr);
        ASSERT(entered != nullptr, "An argument the binding accepted is converted.");
        bound[i].node = entered;
    }

    // The bound values and, unless it is named, the callee: bound to names no program can
    // write, in the scope the expression stands in, so that the new graph captures them the
    // way any closure captures a name. Nothing refers to a node of another graph.
    std::vector<std::string> boundNames;
    for (const Argument &arg : bound) {
        boundNames.push_back(bindTemporary(valueOf(arg.node)));
    }
    const auto make = [&](GCT::node_ptr_t node) {
        node->setOrigin(at);
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
    boundWithOrigin_.erase(target);

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
    // The bound parameters are the new function's captures, not its parameters: each use moves
    // one place down. A bound parameter that was used, or any capture, cannot be published as a
    // Concrete signature here, so the type says Generic instead of promising too little.
    {
        const sem::ResourceSignature &sig = calleeType->resources();
        if (sig.mode == sem::ResourceSignature::Mode::Concrete) {
            bool expressible = sig.captures.empty();
            sem::ResourceSignature remapped = sig;
            remapped.params.clear();
            for (const auto &use : sig.params) {
                if (use.index < bound.size()) {
                    expressible = false;
                    break;
                }
                sem::ResourceSignature::ParamUse moved = use;
                moved.index = static_cast<uint8_t>(use.index - bound.size());
                remapped.params.push_back(std::move(moved));
            }
            if (expressible) {
                factory.setResources(remapped);
            }
        }
    }
    FunctionType *type = factory.build();

    compile_graph_ptr_t graph     = enterScope(type);
    // The graph is the value `f<w>`: it is located at that expression, so the function value
    // made from it covers `f<w>` and not the statement it is written in (1.6).
    registerGraphOrigin(
        context_,
        graph,
        where,
        "gir.partial.graph",
        {},
        {},
        false,
        "",
        exprSpan);
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
            nodeScope_->insert(name, graph->draft().ref(port));
            *with << nameNode(name);
        }
        GCT::node_ptr_t call = make(tree_->makeLink(calleeType->normTypesCount()));
        *call << with;
        index = 0;
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            const std::string name = "$norm" + std::to_string(index++);
            nodeScope_->insert(name, graph->draft().ref(port));
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
    // The same parameters: the forwarder promises what the target promises.
    factory.setResources(targetType->resources());
    FunctionType *type = factory.build();

    compile_graph_ptr_t graph = enterScope(type);
    if (isMacroGraph(target)) {
        // A function value of a macro function stays a macro through the forwarder: a call
        // through the value is still one std::macro may evaluate ahead of time.
        graph->draft().setFlags(static_cast<uint8_t>(
            graph->draft().flags() | camel::runtime::kGraphFlagMacro));
    }
    try {
        auto sourceContext = context_ ? context_->sourceContext() : nullptr;
        auto targetOrigin = sourceContext ? sourceContext->debugMap().graphOrigin(target->graphId())
                                          : camel::source::kInvalidOriginId;
        if (sourceContext && targetOrigin != camel::source::kInvalidOriginId) {
            sourceContext->debugMap().registerGraphOrigin(graph->graphId(), targetOrigin);
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
            linkNodes(LinkType::With, graph->draft().ref(port), call);
        }
        for (draft_node_ref_t port : graph->draft().normPorts()) {
            linkNodes(LinkType::Norm, graph->draft().ref(port), call);
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
    return functionValueOf(valueNodeOf(res), origin);
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
    // The argument is handed to the callee as a resource: the cell is shared with it, so it keeps
    // its cell and is never made SSA (questions-2026-10-24-evening.md, var-version-node: a `var`
    // "handed out as a var argument -- it is a resource"), wherever the callee is written.
    if (arg.expr != nullptr && arg.expr->type() == GCT::NodeKind::DREF) {
        if (const std::string name = moduleLevelName(arg.expr->name()); nodeAt(name).has_value()) {
            if (node_handle_t node = resolveNodeByRef(name); isVariable(node)) {
                escapingVariables_.insert(node);
            }
        }
    }
    if (auto name = immutableNameOf(arg.expr)) {
        diags_->of(SemanticDiag::ImmutablePassedToVarParam)
            .atOrigin(arg.expr->origin())
            .commit(*name, callee);
        throw BuildAbortException();
    }
}

void Builder::evaluateDefaults(
    const GCT::node_ptr_t &decl, FunctionType *funcType, const void *owner,
    bool parametersInScope) {
    if (defaults_.contains(owner)) {
        return;
    }
    const auto *written = GCT::Tree::of(decl)->parameterDefaultsOf(decl);
    if (written == nullptr || written->empty()) {
        return;
    }
    const size_t withCount = funcType->withTypesCount();
    const size_t normCount = funcType->normTypesCount();
    const size_t allCount  = withCount + normCount;
    // A default that reads a parameter of the same declaration is not a constant, and the
    // parameter is only a name where the declaration's own scope is in force. Elsewhere the
    // name is not looked up at all, so that a module-level value of the same name is not taken
    // for it. Both kinds of parameter are checked (design principles 4.4).
    const auto readsParameter = [&](const GCT::node_ptr_t &node, const auto &self) -> bool {
        if (node == nullptr) {
            return false;
        }
        if (node->type() == GCT::NodeKind::DREF) {
            for (size_t i = 0; i < allCount; ++i) {
                if (node->name() == funcType->argNameAt(i)) {
                    return true;
                }
            }
        }
        for (size_t i = 0; i < node->size(); ++i) {
            if (self(node->at(i), self)) {
                return true;
            }
        }
        return false;
    };
    // Evaluated here, where the declaration is, once: the value is remembered and filled into
    // every call that leaves the parameter out (design principles 4.4). A `with` parameter and
    // an ordinary one follow the same rule; only the diagnostic and the parameter list differ.
    const auto evaluate = [&](const std::vector<GCT::Tree::ParameterDefault> &list,
                              size_t count,
                              bool isWith)
        -> std::vector<std::optional<ParameterDefaultValue>> {
        std::vector<std::optional<ParameterDefaultValue>> values(count);
        for (size_t i = 0; i < count && i < list.size(); ++i) {
            const GCT::Tree::ParameterDefault &defaultNode = list[i];
            if (defaultNode.expr == nullptr) {
                continue;
            }
            const std::string name(funcType->argNameAt(isWith ? i : withCount + i));
            Type *paramType = isWith ? funcType->withTypeAt(i) : funcType->normTypeAt(i);
            if (!parametersInScope && readsParameter(defaultNode.expr, readsParameter)) {
                diags_
                    ->of(isWith ? SemanticDiag::WithDefaultNotConstant
                                : SemanticDiag::ParamDefaultNotConstant)
                    .atOrigin(defaultNode.expr->origin())
                    .commit(
                        name,
                        isWith ? "it reads a 'with' parameter of the same declaration"
                               : "it reads a parameter of the same declaration");
                throw BuildAbortException();
            }
            node_handle_t value = visitInPosition(defaultNode.expr, paramType);
            data_ptr_t data      = constantOfNode(value);
            if (data == nullptr) {
                const std::string text = sourceTextOf(defaultNode.expr);
                diags_
                    ->of(isWith ? SemanticDiag::WithDefaultNotConstant
                                : SemanticDiag::ParamDefaultNotConstant)
                    .atOrigin(defaultNode.expr->origin())
                    .commit(
                        name,
                        text.empty() ? std::string("the value is computed at run time")
                                     : "the value of '" + text + "' is computed at run time");
                throw BuildAbortException();
            }
            if (!paramType->assignableFrom(data->type())) {
                data_ptr_t converted = data->convertTo(paramType);
                if (converted == nullptr) {
                    diags_
                        ->of(isWith ? SemanticDiag::WithDefaultWrongType
                                    : SemanticDiag::ParamDefaultWrongType)
                        .atOrigin(defaultNode.expr->origin())
                        .commit(name, data->type()->toString(), paramType->toString());
                    throw BuildAbortException();
                }
                data = converted;
            }
            values[i] = ParameterDefaultValue{data, defaultNode.expr};
        }
        return values;
    };
    defaults_[owner] = DeclarationDefaults{
        evaluate(written->with, withCount, true),
        evaluate(written->norm, normCount, false)};
}

const std::vector<std::optional<Builder::ParameterDefaultValue>> *Builder::defaultsOfCallee(
    node_handle_t callee, bool isWith) const {
    if (callee == nullptr) {
        return nullptr;
    }
    const auto defaultsOf = [&](const void *owner)
        -> const std::vector<std::optional<ParameterDefaultValue>> * {
        const auto it = defaults_.find(owner);
        if (it == defaults_.end()) {
            return nullptr;
        }
        const auto &values = isWith ? it->second.with : it->second.norm;
        return values.empty() ? nullptr : &values;
    };
    // A function value made by explicit type arguments, applied right away: it was instantiated
    // where it was written, and its defaults were kept under the value (visitTypeApplication).
    if (const auto value = defaultsOfValue_.find(callee); value != defaultsOfValue_.end()) {
        const auto &values = isWith ? value->second.with : value->second.norm;
        return values.empty() ? nullptr : &values;
    }
    // A generic function: its declaration is the template, and every instance takes the same
    // defaults.
    if (const auto generic = genericDrefs_.find(callee); generic != genericDrefs_.end()) {
        return defaultsOf(generic->second.one().get());
    }
    if (!nodeIsKind(callee, runtime::GCNodeKind::Dref)) {
        return nullptr;
    }
    const auto &target = nodeGraphOf(callee)->drefTarget(callee);
    if (!std::holds_alternative<graph_vec_ptr_t>(target)) {
        // A native operator: its declaration's defaults are kept under the URI of the group's
        // entry, and each entry is one declaration. A group whose fixed entries are not one
        // signature says nothing about which declaration a call means, and gets none.
        if (!std::holds_alternative<oper_group_ptr_t>(target)) {
            return nullptr;
        }
        FunctionType *same = nullptr;
        const std::string *uri = nullptr;
        for (const auto &[entryUri, resolver] :
             std::get<oper_group_ptr_t>(target)->resolvers()) {
            FunctionType *fixed = resolver ? resolver->fixedSignature() : nullptr;
            if (fixed == nullptr) {
                continue;
            }
            if (same != nullptr && same != fixed) {
                return nullptr;
            }
            same = fixed;
            if (uri == nullptr && nativeDefaults_.contains(entryUri)) {
                uri = &entryUri;
            }
        }
        if (uri == nullptr) {
            return nullptr;
        }
        const auto &values = isWith ? nativeDefaults_.at(*uri).with
                                    : nativeDefaults_.at(*uri).norm;
        return values.empty() ? nullptr : &values;
    }
    const auto graphs = std::get<graph_vec_ptr_t>(target);
    // One declaration: an overload set has one signature per declaration, and a name is not
    // enough to say which one a default belongs to.
    if (!graphs || graphs->size() != 1) {
        return nullptr;
    }
    return defaultsOf(graphs->front().get());
}

std::optional<Builder::CalleeSet> Builder::declarationsOfCallee(node_handle_t callee) const {
    if (callee == nullptr) {
        return std::nullopt;
    }
    // The names of a declaration, filled from the signature it is: one that records none for some
    // position is not one a named argument can reach.
    const auto withNamesOf = [](FunctionType *funcType, CalleeDeclaration &declaration) {
        if (funcType == nullptr || funcType->hasRest() || funcType->hasConstas()) {
            return;
        }
        declaration.readable = camel::core::type::namesOfSignature(
            funcType, declaration.withNames, declaration.normNames);
    };
    CalleeSet set;
    if (const auto generic = genericDrefs_.find(callee); generic != genericDrefs_.end()) {
        for (const auto &candidate : genericOverloadsOf(callee)) {
            if (candidate == nullptr || candidate->funcType == nullptr) {
                return std::nullopt;
            }
            CalleeDeclaration declaration;
            declaration.owner = candidate.get();
            withNamesOf(candidate->funcType, declaration);
            set.label = candidate->name;
            set.declarations.push_back(std::move(declaration));
        }
    } else if (nodeIsKind(callee, runtime::GCNodeKind::Dref)) {
        const auto &target = nodeGraphOf(callee)->drefTarget(callee);
        if (std::holds_alternative<oper_group_ptr_t>(target)) {
            // The rules of an operator group are the declarations of its name (design principles
            // 6.4): whether a call can give an argument by name does not depend on whether the
            // callee is a Camel declaration or a group of rules a C++ module registered. The
            // name has to mark the same position in every rule, exactly as it has to in every
            // declaration of a name, and a rule that cannot place a name (a signature that
            // follows its arguments and spells none, a variadic list, a `rest` parameter) makes
            // the whole call refuse one: the rule is one the name does not reach, and binding
            // the others would bind a position this rule means another thing at.
            const auto &ops = std::get<oper_group_ptr_t>(target);
            for (const auto &[uri, resolver] : ops->resolvers()) {
                CalleeDeclaration declaration;
                declaration.uri = uri;
                if (resolver != nullptr) {
                    declaration.owner = resolver.get();
                    // A rule that fixes its signature answers from it; one whose result follows
                    // its arguments answers from the signature it writes, which is the text
                    // `inspect --ops` prints for it.
                    if (resolver->parameterNames(declaration.withNames, declaration.normNames)) {
                        declaration.readable = true;
                    }
                }
                set.declarations.push_back(std::move(declaration));
            }
            set.label = ops->name();
        } else if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            const auto graphs = std::get<graph_vec_ptr_t>(target);
            if (!graphs) {
                return std::nullopt;
            }
            for (const auto &graph : *graphs) {
                // A decorated declaration is what its decorators return: the call is the decorated
                // one's, and this declaration's parameters are not the ones its arguments are given
                // to, so its names do not say where a named argument goes.
                if (graph == nullptr || graph->funcType() == nullptr ||
                    decoratedOf_.contains(graph.get())) {
                    return std::nullopt;
                }
                CalleeDeclaration declaration;
                declaration.owner = graph.get();
                withNamesOf(graph->funcType(), declaration);
                set.label = graph->name();
                set.declarations.push_back(std::move(declaration));
            }
        } else {
            return std::nullopt;
        }
    } else {
        return std::nullopt;
    }
    // One declaration is not a set: a call of it reads its signature, and the signature path is the
    // one that fills a parameter the call leaves out from the default the declaration registers
    // (`p->run<rules, rounds: rounds, strict: strict>` leaves `explain` out, and `run` has one
    // declaration). A name with none is reported at the call like any other unresolved name. The
    // set path fills no default -- with one declaration it could, but the rule that a default is
    // written by one declaration is the very reason the two paths differ -- so a group with one
    // rule keeps the signature path too, and what a name reaches there is what that path can read
    // (a rule whose signature is fixed; docs/handover/reviews/questions-2026-10-23.md §one-rule-group-names).
    if (set.declarations.size() < 2) {
        return std::nullopt;
    }
    return set;
}

bool Builder::anyDeclarationDefaults(const CalleeSet &set, size_t position, bool isWith) const {
    for (const CalleeDeclaration &declaration : set.declarations) {
        const DeclarationDefaults *values = nullptr;
        if (declaration.owner != nullptr) {
            if (const auto defaults = defaults_.find(declaration.owner);
                defaults != defaults_.end()) {
                values = &defaults->second;
            }
        }
        if (values == nullptr && !declaration.uri.empty()) {
            // A body-less declaration keeps its defaults under its URI (a call to a native
            // operator reaches no graph of the declaration).
            if (const auto native = nativeDefaults_.find(declaration.uri);
                native != nativeDefaults_.end()) {
                values = &native->second;
            }
        }
        if (values == nullptr) {
            continue;
        }
        const auto &list = isWith ? values->with : values->norm;
        if (position < list.size() && list[position].has_value()) {
            return true;
        }
    }
    return false;
}

void Builder::bindNamedArgumentsToDeclarations(
    std::vector<Argument> &inputs, const CalleeSet &set, const GCT::node_ptr_t &call, bool isWith) {
    const auto firstNamed = [&]() -> const Argument & {
        return *std::find_if(inputs.begin(), inputs.end(), [](const Argument &input) {
            return input.name.has_value();
        });
    };
    const std::string &label = set.label;
    const SemanticDiag calleeDiag =
        isWith ? SemanticDiag::WithArgumentCallee : SemanticDiag::ParamArgumentCallee;
    const auto countOf = [&](const CalleeDeclaration &declaration) {
        return isWith ? declaration.withNames.size() : declaration.normNames.size();
    };
    const auto nameOfPosition = [&](const CalleeDeclaration &declaration, size_t position) {
        return isWith ? declaration.withNames[position] : declaration.normNames[position];
    };
    // The name every declaration that has this position gives it; null when they do not agree.
    const auto agreedName = [&](size_t position) -> std::optional<std::string> {
        std::optional<std::string> agreed;
        for (const CalleeDeclaration &declaration : set.declarations) {
            if (position >= countOf(declaration)) {
                continue;
            }
            const std::string name = nameOfPosition(declaration, position);
            if (!agreed.has_value()) {
                agreed = name;
            } else if (*agreed != name) {
                return std::nullopt;
            }
        }
        return agreed;
    };
    // Where each written argument goes. A positional argument is at its own position (the named
    // ones come after the positional ones), and a name places the rest: it has to mean one
    // position in the declarations of the set.
    const size_t written = inputs.size();
    std::vector<size_t> position(written, 0);
    for (size_t at = 0; at < written; ++at) {
        const Argument &input = inputs[at];
        if (!input.name) {
            position[at] = at;
            continue;
        }
        const std::string &name = *input.name;
        std::optional<size_t> one;
        bool elsewhere = false;
        for (const CalleeDeclaration &declaration : set.declarations) {
            for (size_t p = 0; p < countOf(declaration); ++p) {
                if (nameOfPosition(declaration, p) != name) {
                    continue;
                }
                if (!one.has_value()) {
                    one = p;
                } else if (*one != p) {
                    // Two declarations give the name to two parameters: the call's name is not
                    // the name of one parameter of the set.
                    elsewhere = true;
                }
            }
        }
        if (!one.has_value()) {
            diags_
                ->of(isWith ? SemanticDiag::WithArgumentUnknown : SemanticDiag::ParamArgumentUnknown)
                .atOrigin(input.expr->origin())
                .commit(name, label);
            throw BuildAbortException();
        }
        if (elsewhere) {
            diags_
                ->of(SemanticDiag::OverloadParameterNames)
                .atOrigin(input.expr->origin())
                .commit(name, label);
            throw BuildAbortException();
        }
        // The parameter at that position carries the name in every declaration that has it: a
        // declaration that names it otherwise is one the call's name does not reach, and the
        // overloads of a name have to name each position the same way for a named argument to
        // hold for each of them (docs/handover/reviews/questions-2026-10-23.md, 复核：optim 返工
        // 与泛型重载).
        for (const CalleeDeclaration &declaration : set.declarations) {
            if (*one < countOf(declaration) && nameOfPosition(declaration, *one) != name) {
                diags_
                    ->of(SemanticDiag::OverloadParameterNames)
                    .atOrigin(input.expr->origin())
                    .commit(name, label);
                throw BuildAbortException();
            }
        }
        position[at] = *one;
    }
    // The positions the call reaches: a name it gives to a parameter past its own argument count
    // leaves a position below that one empty, and the call then gives no value for it.
    size_t slots = written;
    for (size_t at = 0; at < written; ++at) {
        slots = std::max(slots, position[at] + 1);
    }
    std::vector<bool> taken(slots, false);
    for (size_t at = 0; at < written; ++at) {
        if (taken[position[at]]) {
            const Argument &input = inputs[at];
            diags_
                ->of(isWith ? SemanticDiag::WithArgumentDuplicate
                            : SemanticDiag::ParamArgumentDuplicate)
                .atOrigin(input.expr->origin())
                .commit(*input.name, label);
            throw BuildAbortException();
        }
        taken[position[at]] = true;
    }
    // A call fills the first positions or none of them: one left empty is a parameter it gives no
    // value for. The set cannot supply the value -- a default is written by one declaration, and
    // the call does not say which one it means -- so a parameter a declaration defaults is refused
    // here too. Nothing is reached past this loop with an empty position: fewer arguments than
    // positions leave one, so the loop either reports or every position is filled.
    for (size_t p = 0; p < slots; ++p) {
        if (taken[p]) {
            continue;
        }
        const std::optional<std::string> name = agreedName(p);
        if (!name.has_value() || anyDeclarationDefaults(set, p, isWith)) {
            const Argument &input = firstNamed();
            diags_->of(calleeDiag).atOrigin(input.expr->origin()).commit(*input.name, label);
            throw BuildAbortException();
        }
        diags_
            ->of(isWith ? SemanticDiag::WithArgumentMissing : SemanticDiag::ParamArgumentMissing)
            .atOrigin(call->origin())
            .commit(*name, label);
        throw BuildAbortException();
    }
    std::vector<Argument> ordered(slots);
    for (size_t at = 0; at < written; ++at) {
        ordered[position[at]] = inputs[at];
    }
    inputs = std::move(ordered);
}

void Builder::bindArguments(
    std::vector<Argument> &inputs, node_handle_t callee, const GCT::node_ptr_t &call,
    FunctionType *signature, bool isWith) {
    const bool named = std::any_of(
        inputs.begin(), inputs.end(), [](const Argument &input) { return input.name.has_value(); });
    // `signature` is the call's own reading of the callee (signatureForArgumentContext), so
    // the names below are resolved against exactly the signature the call resolves against.
    //
    // A callee that is several declarations is read as the declarations themselves, when they
    // record their names: the parameter at a position carries the same name in every one of them,
    // which is what lets one named argument mean the same parameter in each of them
    // (docs/handover/reviews/questions-2026-10-23.md, 复核：optim 返工与泛型重载). The signature a
    // call of several declarations does have -- the one entry whose parameters are the same as the
    // others' -- answers for the types of the arguments, not for which parameter a name is.
    const std::optional<CalleeSet> declarations = declarationsOfCallee(callee);
    bool readable                               = declarations.has_value();
    if (readable) {
        for (const CalleeDeclaration &declaration : declarations->declarations) {
            // A `rest` parameter and a `constas` parameter are written at their own position, not
            // by name, and a declaration that records no name for a position answers nothing about
            // it: one such declaration is one the call's name does not reach.
            if (!declaration.readable) {
                readable = false;
                break;
            }
        }
    }
    if (named && readable) {
        bindNamedArgumentsToDeclarations(inputs, *declarations, call, isWith);
        return;
    }
    std::string label;
    if (signature == nullptr) {
        if (!named) {
            return;
        }
        const Argument &first = *std::find_if(
            inputs.begin(), inputs.end(),
            [](const Argument &input) { return input.name.has_value(); });
        diags_
            ->of(isWith ? SemanticDiag::WithArgumentCallee : SemanticDiag::ParamArgumentCallee)
            .atOrigin(first.expr->origin())
            .commit(*first.name, declarations ? declarations->label : call->at(0)->toString());
        throw BuildAbortException();
    }
    const size_t count = isWith ? signature->withTypesCount() : signature->normTypesCount();
    if (!named) {
        // Only a call that leaves a parameter out, or gives too many, has anything to bind
        // here; the arguments of a complete positional call are already in place.
        if (inputs.size() >= count) {
            // More arguments than parameters: the call reports the mismatch as it always has.
            return;
        }
    }
    if (nodeIsKind(callee, runtime::GCNodeKind::Dref)) {
        const auto &target = nodeGraphOf(callee)->drefTarget(callee);
        if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            const auto graphs = std::get<graph_vec_ptr_t>(target);
            if (graphs && graphs->size() == 1) {
                label = graphs->front()->name();
            }
        }
    }
    if (label.empty()) {
        label = signature->toString();
    }
    const auto *defaults = defaultsOfCallee(callee, isWith);
    // The name of the i-th parameter of the declaration, whichever list it is in: `with`
    // parameters first, then the ordinary ones, as the signature keeps them.
    const auto nameAt = [&](size_t i) {
        return std::string(signature->argNameAt(isWith ? i : signature->withTypesCount() + i));
    };
    if (named && !signature->hasArgNames()) {
        // A signature without names is not one a call can give arguments by name to: the
        // resolution reports the arguments instead.
        const Argument &first =
            *std::find_if(inputs.begin(), inputs.end(), [](const Argument &input) {
                return input.name.has_value();
            });
        diags_
            ->of(isWith ? SemanticDiag::WithArgumentCallee : SemanticDiag::ParamArgumentCallee)
            .atOrigin(first.expr->origin())
            .commit(*first.name, label);
        throw BuildAbortException();
    }
    std::vector<Argument> ordered(count);
    std::vector<bool> filled(count, false);
    size_t positional = 0;
    for (const Argument &input : inputs) {
        size_t index = 0;
        if (input.name) {
            bool found = false;
            for (size_t i = 0; i < count; ++i) {
                if (*input.name == nameAt(i)) {
                    index = i;
                    found = true;
                    break;
                }
            }
            if (!found) {
                diags_
                    ->of(isWith ? SemanticDiag::WithArgumentUnknown
                                : SemanticDiag::ParamArgumentUnknown)
                    .atOrigin(input.expr->origin())
                    .commit(*input.name, label);
                throw BuildAbortException();
            }
            if (filled[index]) {
                diags_
                    ->of(isWith ? SemanticDiag::WithArgumentDuplicate
                                : SemanticDiag::ParamArgumentDuplicate)
                    .atOrigin(input.expr->origin())
                    .commit(*input.name, label);
                throw BuildAbortException();
            }
        } else {
            index = positional++;
            if (index >= count) {
                // A positional argument past the last parameter: the count mismatch below is
                // the older, more precise report for it, and a name can no longer be placed.
                return;
            }
        }
        ordered[index] = input;
        filled[index]   = true;
    }
    for (size_t i = 0; i < count; ++i) {
        if (filled[i]) {
            continue;
        }
        const bool hasDefault =
            defaults != nullptr && i < defaults->size() && (*defaults)[i].has_value();
        if (!hasDefault) {
            if (!named) {
                // A call written with too few positional arguments: the count mismatch the
                // call reports below is the older, more precise report for it.
                return;
            }
            diags_
                ->of(isWith ? SemanticDiag::WithArgumentMissing
                            : SemanticDiag::ParamArgumentMissing)
                .atOrigin(call->origin())
                .commit(nameAt(i), label);
            throw BuildAbortException();
        }
        // The default is one constant of the declaration: the call gets a static node holding
        // it, and no expression is evaluated here (design principles 4.4). Two calls in one
        // graph that leave the same parameter out share that node: the value was computed once.
        const ParameterDefaultValue &value = *(*defaults)[i];
        node_handle_t dataNode{};
        if (const auto graph = defaultNodes_.find(currGraph_.get()); graph != defaultNodes_.end()) {
            if (const auto found = graph->second.find(value.expr);
                found != graph->second.end()) {
                dataNode = found->second;
            }
        }
        if (dataNode == nullptr) {
            dataNode = currGraph_->addStaticDataNode(value.data);
            rememberConstant(dataNode, value.data);
            registerNodeOrigin(context_, dataNode, value.expr, "gir.data.static.paramdefault");
            defaultNodes_[currGraph_.get()][value.expr] = dataNode;
        }
        ordered[i] = Argument{dataNode, value.expr, std::nullopt};
        filled[i]  = true;
    }
    inputs = std::move(ordered);
    // The call now reads its arguments by the declaration's parameter order, and the types are
    // built from the nodes in that order by the caller, once every deferred argument has been
    // built. Nothing here fills a type list: the two would diverge.
}

} // namespace camel::compile::gir
