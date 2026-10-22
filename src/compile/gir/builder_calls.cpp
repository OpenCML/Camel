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
        // statement the binding is written in (H9 1.6).
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
    // principles 4.4, 5.1; H27d): a function whose `with` parameters take the bound values
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
    // candidates in the pass that is used are an ambiguity (design principles 5.1, H27e): it
    // is recorded here and reported when the binding is refused for good, because the position
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
    // made from it covers `f<w>` and not the statement it is written in (H9 1.6).
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
    if (auto name = immutableNameOf(arg.expr)) {
        diags_->of(SemanticDiag::ImmutablePassedToVarParam)
            .atOrigin(arg.expr->origin())
            .commit(*name, callee);
        throw BuildAbortException();
    }
}

} // namespace camel::compile::gir
