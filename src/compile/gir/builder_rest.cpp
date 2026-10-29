// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Rest parameters (design principles 5.5, step 6). A function with a rest parameter has no
 * runtime arity, so it is not given a graph and is not a value: each direct call expands its
 * body into the caller's graph (inlineRestCall). The fixed parameters are bound to the nodes of
 * the arguments, and the rest parameter is bound to the nodes the call leaves over, whether
 * written out or expanded from `..e`. The expansion is straight-line code in the caller: no
 * call node, no instance and no runtime tuple for `..ts`.
 */

#include "builder.h"
#include "camel/core/type/typecall.h"
#include "camel/utils/str.h"
#include "builder_helpers.h"
#include "builder_support.h"

#include "camel/core/data/composite/tuple.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/tuple.h"

#include <algorithm>

#define DEBUG_LEVEL -1

namespace camel::compile::gir {

using namespace camel::core::error;
using namespace camel::core::type;

namespace {
/// Whether the type still holds an expansion (`..E`, design principles 5.6) that nothing bound.
/// An expansion a call bound is expanded away (substituteTypeVars), so a node still here has no
/// members to write: the result type was written over a pack the call gave nothing.
bool holdsPackExpansion(Type *type) {
    if (type == nullptr) {
        return false;
    }
    if (TypeCallType::isExpand(type)) {
        return true;
    }
    switch (type->code()) {
    case TypeCode::Array:
        return holdsPackExpansion(static_cast<ArrayType *>(type)->elemType());
    case TypeCode::Tuple:
        for (Type *member : static_cast<TupleType *>(type)->types()) {
            if (holdsPackExpansion(member)) {
                return true;
            }
        }
        return false;
    default:
        break;
    }
    if (type->isOtherType()) {
        for (Type *param : static_cast<OtherType *>(type)->paramsSpan()) {
            if (holdsPackExpansion(param)) {
                return true;
            }
        }
    }
    return false;
}
} // namespace

const Builder::RestBinding *Builder::restBindingNamed(const std::string &name) const {
    // A local of the same name shadows the rest parameter, exactly as a parameter does.
    if (nodeScope_->has(name, false)) {
        return nullptr;
    }
    for (auto it = restBindings_.rbegin(); it != restBindings_.rend(); ++it) {
        if (it->name == name) {
            return &*it;
        }
    }
    return nullptr;
}

std::vector<node_handle_t> Builder::expandRestArgument(
    const GCT::node_ptr_t &operand, const std::string &written, const GCT::node_ptr_t &call) {
    // A build-time tuple (the rest parameter being forwarded, or a tuple comprehension) gives
    // its elements directly: no FILL is built for it.
    const std::any operandRes = visit(operand);
    if (operandRes.type() == typeid(StaticTuple)) {
        return any_cast<const StaticTuple &>(operandRes).elements;
    }
    const node_handle_t value = valueNodeOf(operandRes);
    if (value == nullptr) {
        diags_->of(SemanticDiag::RestArgumentNotTuple)
            .atOrigin(operand->origin() != 0 ? operand->origin() : call->origin())
            .commit(written);
        throw BuildAbortException();
    }
    Type *type = valueTypeOf(nodeTypeOf(value));
    // A tuple comprehension is packed into a FILL whose With inputs are all its elements. A
    // tuple literal has already been expanded at GCT, so a FILL here has no constant slots.
    if (nodeIsKind(value, runtime::GCNodeKind::Fill) && type->code() == TypeCode::Tuple) {
        const auto payload = nodeDraftOf(value)->payloadOf(nodeIdOf(value));
        if (payload.size() >= sizeof(runtime::GCFillBody)) {
            const auto *fill = reinterpret_cast<const runtime::GCFillBody *>(payload.data());
            const auto with  = withInputsOf(value);
            if (fill->fillKind == runtime::GCFillKind::Tuple &&
                with.size() == tt::as_ptr<TupleType>(type)->size()) {
                std::vector<node_handle_t> values;
                values.reserve(with.size());
                for (const auto ref : with) {
                    values.emplace_back(nodeDraftOf(value), ref);
                }
                return values;
            }
        }
    }
    diags_->of(SemanticDiag::RestArgumentNotTuple)
        .atOrigin(operand->origin() != 0 ? operand->origin() : call->origin())
        .commit(written);
    throw BuildAbortException();
}

node_handle_t Builder::inlineRestCall(
    const generic_template_ptr_t &tmpl, std::vector<Argument> &withInputs,
    std::vector<Argument> &normInputs, const GCT::node_ptr_t &call, Type *expectedResult) {
    FunctionType *signature = tmpl->funcType;
    const size_t withCount  = signature->withTypesCount();
    const size_t declaredNormCount = signature->normTypesCount();
    // The runtime parameters: the `constas` ones are compile-time values and are dropped from
    // the arguments below (design principles 5.4); the rest parameter is always last.
    const size_t normCount  = declaredNormCount - signature->normConstasCount();
    ASSERT(signature->hasRest() && normCount > 0 && restBindings_.size() < 64,
           "A rest signature has its rest parameter last.");
    const size_t fixedCount = normCount - 1;
    const std::string restName(signature->argNameAt(withCount + declaredNormCount - 1));
    // The declared ordinary parameter of each runtime argument.
    std::vector<size_t> runtimeIndices;
    runtimeIndices.reserve(declaredNormCount);
    for (size_t i = 0; i < declaredNormCount; ++i) {
        if (!signature->normIsConstasAt(i)) {
            runtimeIndices.push_back(i);
        }
    }

    if (withInputs.size() != withCount) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(call->origin())
            .commit(signature->toString(), "the with-arguments given at the call");
        throw BuildAbortException();
    }
    if (signature->withConstasCount() > 0) {
        diags_->of(SemanticDiag::ArgumentsMismatch)
            .atOrigin(call->origin())
            .commit(signature->toString(), "a rest function takes no 'constas' among its with parameters");
        throw BuildAbortException();
    }
    ConstasValues constas;
    ExtentValues extents;
    takeConstasValues(signature, withInputs, nullptr, normInputs, nullptr, call, constas, extents);
    if (normInputs.size() < fixedCount) {
        diags_->of(SemanticDiag::RestArgumentsMismatch)
            .atOrigin(call->origin())
            .commit(tmpl->name, normInputs.size(), fixedCount);
        throw BuildAbortException();
    }
    // A rest function of another module is expanded here too: its body is built where it was
    // declared (its names resolve there) but into the caller's graph, exactly like one of this
    // module. A body that names a value of its own module has no node here, and the reference
    // to it is refused where it is built (visitDRefNode), not by refusing the whole call.

    // The fixed arguments bind the type variables and are held to their parameters here, where
    // the expansion replaces the ordinary call.
    type_bindings_t bindings = tmpl->outerBindings;
    // A `with`-argument that names a generic function or a set of overloads is a compile-time
    // callable, not a value: the instance is inlined (design principles 5.5 Q1/Q3), so
    // the name stays a name and every call of the parameter in the body resolves it against
    // that call's argument types, one leaf at a time (`tree_map<twice>`). It has no type of
    // its own to bind the parameter's type variables with.
    std::vector<bool> callableName(withCount, false);
    const auto isCallableName = [&](const Argument &argument) {
        if (genericDrefs_.contains(argument.node)) {
            return true;
        }
        if (argument.node == nullptr || !nodeIsKind(argument.node, runtime::GCNodeKind::Dref) ||
            boundWithArgs_.contains(argument.node)) {
            return false;
        }
        const auto &target = nodeGraphOf(argument.node)->drefTarget(argument.node);
        if (std::holds_alternative<graph_vec_ptr_t>(target)) {
            auto graphs = asCompileGraphVec(target);
            return graphs && graphs->size() != 1;
        }
        return std::holds_alternative<oper_group_ptr_t>(target);
    };
    const auto bindOne = [&](Type *pattern, Argument &argument, bool isWith, size_t index) {
        Type *actual = valueTypeOf(nodeTypeOf(argument.node));
        if (isWith && isCallableName(argument)) {
            callableName[index] = true;
            return;
        }
        if (genericDrefs_.contains(argument.node)) {
            diags_->of(SemanticDiag::GenericArgumentsMismatch)
                .atOrigin(argument.expr != nullptr ? argument.expr->origin() : call->origin())
                .commit(
                    tmpl->name,
                    actual->toString(),
                    "a rest function's fixed parameter takes a value, not a generic function",
                    signature->toString());
            throw BuildAbortException();
        }
        if (!bindTypeVars(pattern, actual, bindings)) {
            diags_->of(SemanticDiag::GenericArgumentsMismatch)
                .atOrigin(argument.expr != nullptr ? argument.expr->origin() : call->origin())
                .commit(
                    tmpl->name,
                    actual->toString(),
                    isWith ? "the with-argument does not have the form of its parameter"
                           : "the argument does not have the form of its parameter",
                    signature->toString());
            throw BuildAbortException();
        }
    };
    for (size_t i = 0; i < withCount; ++i) {
        bindOne(signature->withTypeAt(i), withInputs[i], true, i);
    }
    for (size_t i = 0; i < fixedCount; ++i) {
        Type *pattern = substituteExtentValues(signature->normTypeAt(runtimeIndices[i]), extents);
        bindOne(pattern, normInputs[i], false, i);
    }
    // Every type variable of the fixed parameters must be known: the rest parameter gives no
    // types of its own, and the body cannot be built around an open `T`.
    for (size_t i = 0; i < withCount; ++i) {
        if (callableName[i]) {
            continue;
        }
        if (containsTypeVar(substituteTypeVars(signature->withTypeAt(i), bindings))) {
            diags_->of(SemanticDiag::GenericArgumentsMismatch)
                .atOrigin(call->origin())
                .commit(
                    tmpl->name,
                    signature->toString(),
                    "a type variable is not determined by the fixed arguments",
                    signature->toString());
            throw BuildAbortException();
        }
    }
    for (size_t i = 0; i < fixedCount; ++i) {
        Type *wanted = substituteExtentValues(
            substituteTypeVars(signature->normTypeAt(runtimeIndices[i]), bindings), extents);
        if (containsTypeVar(wanted)) {
            diags_->of(SemanticDiag::GenericArgumentsMismatch)
                .atOrigin(call->origin())
                .commit(
                    tmpl->name,
                    signature->toString(),
                    "a type variable is not determined by the fixed arguments",
                    signature->toString());
            throw BuildAbortException();
        }
        node_handle_t entered = checkValue(normInputs[i].node, wanted, normInputs[i].expr);
        if (entered == nullptr) {
            diags_->of(SemanticDiag::ArgumentsMismatch)
                .atOrigin(normInputs[i].expr != nullptr ? normInputs[i].expr->origin()
                                                        : call->origin())
                .commit(
                    signature->toString(),
                    valueTypeOf(nodeTypeOf(normInputs[i].node))->toString());
            throw BuildAbortException();
        }
        normInputs[i].node = entered;
    }
    for (size_t i = 0; i < withCount; ++i) {
        if (callableName[i]) {
            // The name is inserted into the body's scope as it is; the call of it resolves
            // the generic function or the overload set.
            continue;
        }
        Type *wanted = substituteTypeVars(signature->withTypeAt(i), bindings);
        node_handle_t entered = checkValue(withInputs[i].node, wanted, withInputs[i].expr);
        if (entered == nullptr) {
            diags_->of(SemanticDiag::ArgumentsMismatch)
                .atOrigin(withInputs[i].expr != nullptr ? withInputs[i].expr->origin()
                                                        : call->origin())
                .commit(signature->toString(), valueTypeOf(nodeTypeOf(withInputs[i].node))->toString());
            throw BuildAbortException();
        }
        withInputs[i].node = entered;
    }
    // A `constas` value is held to the type of its parameter like a fixed argument is, with the
    // other values' extents and the type variables the fixed arguments bound (5.4).
    for (size_t i = 0; i < declaredNormCount; ++i) {
        if (!signature->normIsConstasAt(i)) {
            continue;
        }
        const std::string name(signature->argNameAt(withCount + i));
        ConstasValue &value = constas.at(name);
        Type *wanted =
            substituteExtentValues(substituteTypeVars(signature->normTypeAt(i), bindings), extents);
        if (!wanted->assignableFrom(value.valueType) &&
            !entersImplicitly(wanted, value.valueType)) {
            diags_->of(SemanticDiag::ArgumentsMismatch)
                .atOrigin(call->origin())
                .commit(signature->toString(), value.valueType->toString());
            throw BuildAbortException();
        }
        value.type = wanted;
    }

    std::vector<node_handle_t> restValues;
    restValues.reserve(normInputs.size() - fixedCount);
    type_vec_t restTypes;
    restTypes.reserve(normInputs.size() - fixedCount);
    for (size_t i = fixedCount; i < normInputs.size(); ++i) {
        restValues.push_back(normInputs[i].node);
        restTypes.push_back(valueTypeOf(nodeTypeOf(normInputs[i].node)));
    }
    // A rest parameter typed `..xs: ..A` binds `A` to the pack of the types it took, the same
    // binding a call of a declaration without a body makes (DeclaredFuncTypeResolver).
    if (Type *pack = signature->restPack()) {
        bindings.emplace_back(
            static_cast<camel::core::type::TypeVarType *>(pack)->name(),
            camel::core::type::TypeCallType::makePack(restTypes));
    }
    // The declaration's `when`, now that the fixed arguments and the pack have their types: a
    // false condition makes the function inapplicable (design principles 4.4).
    if (const auto *when = GCT::Tree::of(tmpl->func)->whenOf(tmpl->func)) {
        std::unordered_map<std::string, data_ptr_t> withValues;
        for (size_t i = 0; i < withCount; ++i) {
            if (data_ptr_t value = constantOfNode(withInputs[i].node); value != nullptr) {
                withValues.emplace(std::string(signature->argNameAt(i)), value);
            }
        }
        const WhenResult verdict =
            evaluateWhenCondition(when->condition, signature, bindings, withValues, tmpl);
        if (verdict.fails()) {
            yieldToPipe(call);
            std::vector<Type *> all;
            for (const Argument &input : withInputs) {
                all.push_back(valueTypeOf(nodeTypeOf(input.node)));
            }
            for (const Argument &input : normInputs) {
                all.push_back(valueTypeOf(nodeTypeOf(input.node)));
            }
            diags_->of(SemanticDiag::WhenConditionUnmet)
                .atOrigin(call->origin())
                .commit(
                    tmpl->name,
                    strutil::join(all, ", ", [](Type *t) { return t->toString(); }),
                    tmpl->name + " — when " + when->text);
            throw BuildAbortException();
        }
        if (!verdict.holds()) {
            reportWhenProblem(verdict, tmpl->name, when->condition);
        }
    }
    RestBinding rest;
    rest.name                 = restName;
    rest.tuple.type           = TupleType::create(std::move(restTypes));
    rest.tuple.elements       = std::move(restValues);
    rest.tuple.materializable = false;
    rest.tuple.origin         = call;

    Type *exitType = signature->hasExitType()
                         ? substituteExtentValues(
                               substituteTypeVars(signature->exitType(), bindings), extents)
                         : nullptr;
    if (exitType != nullptr && holdsPackExpansion(exitType)) {
        // The result type is written over a pack nothing bound: the declaration declares one at
        // its head without a rest parameter that gives it members, so this call has no types to
        // write the expansion with.
        std::string what = exitType->toString();
        std::string why  = "the call leaves no argument for the pack to range over";
        describeUnevaluatedTypeCall(exitType, what, why);
        diags_->of(SemanticDiag::PackExpansionNeedsOnePack)
            .atOrigin(call->origin())
            .commit(what, why);
        throw BuildAbortException();
    }
    if (exitType != nullptr && containsTypeVar(exitType)) {
        diags_->of(SemanticDiag::GenericArgumentsMismatch)
            .atOrigin(call->origin())
            .commit(
                tmpl->name,
                signature->toString(),
                "the result type holds a type variable that the fixed arguments do not determine",
                signature->toString());
        throw BuildAbortException();
    }

    if (instantiationDepth_ >= kMaxInstantiationDepth) {
        diags_->of(SemanticDiag::GenericInstantiationCycle)
            .atOrigin(call->origin())
            .commit(tmpl->name);
        throw BuildAbortException();
    }

    // The expansion is built where the function was declared (its names resolve there), but
    // into the caller's graph: there is no instance graph and no call node.
    struct ExpansionState {
        Builder &self;
        compile_graph_ptr_t graph;
        node_scope_ptr_t nodeScope;
        graph_scope_ptr_t graphScope;
        decorated_scope_ptr_t decoratedScope;
        template_scope_ptr_t templateScope;
        type_bindings_t bindings;
        bool synced, varied, declaresVariable, calleeAllowed;
        node_handle_t lastSynced;
        const GCT::Node *syncedOverride, *unused;
        std::pair<GCT::node_ptr_t, node_handle_t> lastBlock;
        StaticBodyScope *staticBody;
        size_t staticDepth;
        RestBody *restBody;
        size_t restCount;
        size_t staticValueCount;
        Type *expected;
        camel::core::module::module_ptr_t module;
        std::vector<std::string> namespacePath;
        std::vector<const std::vector<Reference> *> usingFrames;
        ExpansionState(Builder &b)
            : self(b), graph(b.currGraph_), nodeScope(b.nodeScope_), graphScope(b.graphScope_),
              decoratedScope(b.decoratedScope_), templateScope(b.templateScope_),
              bindings(b.typeBindings_), synced(b.synced_),
              varied(b.varied_), declaresVariable(b.declaresVariable_),
              calleeAllowed(b.genericCalleeAllowed_), lastSynced(b.lastSyncedNode_),
              syncedOverride(b.syncedBlockOverride_),
              unused(b.unusedStatement_), lastBlock(b.lastBlockStatement_),
              staticBody(std::exchange(b.activeStaticBody_, nullptr)),
              staticDepth(std::exchange(b.staticDepth_, 0)),
              restBody(std::exchange(b.activeRestBody_, nullptr)),
              restCount(b.restBindings_.size()), staticValueCount(b.staticValueScopes_.size()),
              expected(b.expectedValueType_),
              module(b.module_), namespacePath(b.namespacePath_), usingFrames(b.usingFrames_) {
            ++self.instantiationDepth_;
        }
        ~ExpansionState() {
            --self.instantiationDepth_;
            self.currGraph_            = graph;
            self.nodeScope_            = nodeScope;
            self.graphScope_           = graphScope;
            self.decoratedScope_       = decoratedScope;
            self.templateScope_        = templateScope;
            self.typeBindings_         = std::move(bindings);
            self.synced_               = synced;
            self.varied_               = varied;
            self.declaresVariable_     = declaresVariable;
            self.genericCalleeAllowed_ = calleeAllowed;
            self.lastSyncedNode_       = lastSynced;
            self.syncedBlockOverride_  = syncedOverride;
            self.unusedStatement_      = unused;
            self.lastBlockStatement_   = lastBlock;
            self.activeStaticBody_     = staticBody;
            self.staticDepth_          = staticDepth;
            self.activeRestBody_       = restBody;
            self.restBindings_.resize(restCount);
            self.staticValueScopes_.resize(staticValueCount);
            self.expectedValueType_    = expected;
            self.module_               = module;
            self.namespacePath_        = std::move(namespacePath);
            self.usingFrames_          = std::move(usingFrames);
        }
    } state(*this);

    AttrScope attrs(currentAttrs_, tmpl->attrs);
    currGraph_      = tmpl->ownerGraph; // only while the scopes are entered; see below
    module_         = tmpl->module;
    namespacePath_  = tmpl->namespacePath;
    usingFrames_    = tmpl->usingFrames;
    nodeScope_      = tmpl->nodeScope;
    graphScope_     = tmpl->graphScope;
    decoratedScope_ = tmpl->decoratedScope;
    templateScope_  = tmpl->templateScope;
    typeBindings_   = tmpl->outerBindings;
    typeBindings_.insert(typeBindings_.end(), bindings.begin(), bindings.end());
    synced_               = state.synced;
    varied_               = false;
    declaresVariable_     = false;
    genericCalleeAllowed_ = false;
    // Expand into the caller's graph, not into the graph the function was declared in.
    currGraph_            = state.graph;
    expectedValueType_    = exitType != nullptr ? exitType : expectedResult;
    activeRestBody_       = nullptr;
    restBindings_.push_back(std::move(rest));

    RestBody bodyScope;
    bodyScope.exitType = exitType;
    activeRestBody_    = &bodyScope;
    const bool outerSynced = synced_;
    if (outerSynced) {
        syncedBlockOverride_ = tmpl->func->at(1);
    }

    enterBlockScope();
    // The values of the `constas` parameters are static values of the expanded body, like the
    // `n` of a `static for`: they are not runtime arguments and have no node of their own.
    staticValueScopes_.emplace_back();
    for (const auto &[constasName, constasValue] : constas) {
        staticValueScopes_.back().push_back(
            {constasName, constasKeyOf(constasValue.data), constasValue.node});
    }
    try {
        for (size_t i = 0; i < withCount; ++i) {
            if (!insertNode(std::string(signature->argNameAt(i)), withInputs[i].node)) {
                diags_->of(SemanticDiag::DuplicateParameter)
                    .atOrigin(call->origin())
                    .commit(std::string(signature->argNameAt(i)));
                throw BuildAbortException();
            }
        }
        for (size_t i = 0; i < fixedCount; ++i) {
            const std::string name(signature->argNameAt(withCount + runtimeIndices[i]));
            if (!insertNode(name, normInputs[i].node)) {
                diags_->of(SemanticDiag::DuplicateParameter)
                    .atOrigin(call->origin())
                    .commit(name);
                throw BuildAbortException();
            }
        }
        node_handle_t bodyResult = visitExecNode(
            tmpl->func->at(1), exitType != nullptr ? exitType : expectedResult);
        if (bodyScope.returned) {
            bodyResult = bodyScope.value;
        }
        if (exitType != nullptr && exitType->code() != TypeCode::Void && bodyResult != nullptr) {
            node_handle_t entered = checkValue(bodyResult, exitType, call);
            if (entered == nullptr) {
                diags_->of(SemanticDiag::ReturnTypeMismatch)
                    .atOrigin(call->origin())
                    .commit(
                        valueTypeOf(nodeTypeOf(bodyResult))->toString(),
                        exitType->toString(),
                        tmpl->name + ": " + signature->toString());
                throw BuildAbortException();
            }
            bodyResult = entered;
        }
        if (exitType != nullptr && exitType->code() == TypeCode::Void) {
            bodyResult = nullptr;
        }
        leaveBlockScope();
        LEAVE("REST");
        return bodyResult;
    } catch (...) {
        leaveBlockScope();
        throw;
    }
}

} // namespace camel::compile::gir