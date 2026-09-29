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
 * Author: Camel Contributors
 * Created: Sep. 29, 2026
 * Updated: Sep. 29, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Pullback transform (see pullback.h).
 */

#include "pullback.h"

#include "rules.h"

#include "camel/core/derivative.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_session.h"
#include "camel/utils/type.h"

#include <atomic>
#include <deque>
#include <format>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace camel::autodiff {

namespace {

namespace mm = camel::core::mm;
namespace rt = camel::runtime;
using ::Modifier;
using camel::core::aggregateElement;
using camel::core::aggregateSize;
using camel::core::DerivativeRegistry;
using camel::core::isAggregate;
using camel::core::tangentElementIndex;
using camel::core::TangentSpace;
using camel::core::tangentTypeOf;
using camel::core::vjp_node_t;
using camel::core::VjpBuilder;
using camel::core::VjpCall;
using camel::core::error::RuntimeDiag;
using camel::core::error::throwRuntimeFault;
using camel::core::rtdata::fromSlot;
using camel::core::rtdata::toSlot;
using rt::gc_node_ref_t;
using rt::GCGraph;
using rt::GCNodeKind;
using rt::GraphDraft;
using rt::kInvalidNodeRef;
using type::FunctionType;
using type::param_vec_t;
using type::StructType;
using type::TupleType;
using type::Type;
using type::TypeCode;

[[noreturn]] void fail(const std::string &message) {
    throwRuntimeFault(RuntimeDiag::RuntimeError, "autodiff: " + message);
}

// ---------------------------------------------------------------- draft helpers

Type *typeOf(const GraphDraft &draft, gc_node_ref_t node) {
    const auto *header = draft.header(node);
    ASSERT(header != nullptr, "autodiff: node lookup failed.");
    return header->dataType;
}

GCNodeKind kindOf(const GraphDraft &draft, gc_node_ref_t node) {
    const auto *header = draft.header(node);
    ASSERT(header != nullptr, "autodiff: node lookup failed.");
    return header->kind;
}

std::vector<gc_node_ref_t> copyOf(std::span<const gc_node_ref_t> refs) {
    return {refs.begin(), refs.end()};
}

std::optional<slot_t> staticValue(const GraphDraft &draft, gc_node_ref_t node) {
    const auto *header = draft.header(node);
    if (!header || header->kind != GCNodeKind::Data || header->dataIndex >= 0) {
        return std::nullopt;
    }
    return draft.staticSlots()[static_cast<size_t>(-header->dataIndex)];
}

gc_node_ref_t funcCallee(const GraphDraft &draft, gc_node_ref_t node, GCGraph **callee) {
    const auto payload = draft.payloadOf(node);
    ASSERT(payload.size_bytes() >= sizeof(rt::GCFuncBody), "autodiff: FUNC payload missing.");
    *callee = reinterpret_cast<const rt::GCFuncBody *>(payload.data())->calleeGraph;
    return node;
}

GCGraph *calleeOf(const GraphDraft &draft, gc_node_ref_t node) {
    GCGraph *callee = nullptr;
    funcCallee(draft, node, &callee);
    return callee;
}

gc_node_ref_t addAccs(GraphDraft &draft, Type *type, gc_node_ref_t source, size_t index) {
    const gc_node_ref_t node = draft.addAccsNode(type, static_cast<uint32_t>(index));
    draft.setNormInputs(node, std::span<const gc_node_ref_t>(&source, 1));
    return node;
}

gc_node_ref_t addField(GraphDraft &draft, Type *aggregate, gc_node_ref_t source, size_t index) {
    Type *elemType = aggregateElement(aggregate, index);
    if (aggregate->code() == TypeCode::Struct) {
        const auto name          = tt::as_ptr<StructType>(aggregate)->fieldName(index);
        const gc_node_ref_t node = draft.addAccsNode(elemType, name);
        draft.setNormInputs(node, std::span<const gc_node_ref_t>(&source, 1));
        return node;
    }
    return addAccs(draft, elemType, source, index);
}

gc_node_ref_t
addOper(GraphDraft &draft, Type *type, std::string_view uri, std::span<const gc_node_ref_t> in) {
    const gc_node_ref_t node = draft.addOperNode(type, nullptr, uri);
    draft.setNormInputs(node, in);
    return node;
}

gc_node_ref_t
addCall(GraphDraft &draft, Type *type, gc_node_ref_t callee, std::span<const gc_node_ref_t> args) {
    const gc_node_ref_t node = draft.addCallNode(type);
    draft.setWithInputs(node, std::span<const gc_node_ref_t>(&callee, 1));
    draft.setNormInputs(node, args);
    return node;
}

gc_node_ref_t addFill(
    GraphDraft &draft, Type *type, rt::GCFillKind kind, gc_node_ref_t source,
    std::span<const gc_node_ref_t> values) {
    std::vector<size_t> slots(values.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        slots[i] = i;
    }
    const auto payload       = rt::makeFillPayload(kind, slots);
    const gc_node_ref_t node = draft.addFillNode(type, payload);
    draft.setNormInputs(node, std::span<const gc_node_ref_t>(&source, 1));
    draft.setWithInputs(node, values);
    return node;
}

/// A tuple or struct built from `values`, one per element in order.
gc_node_ref_t addAggregate(GraphDraft &draft, Type *type, std::span<const gc_node_ref_t> values) {
    const size_t size = aggregateSize(type);
    ASSERT(values.size() == size, "autodiff: aggregate element count mismatch.");
    camel::core::rtdata::Object *empty = nullptr;
    rt::GCFillKind kind;
    if (type->code() == TypeCode::Struct) {
        empty = ::Struct::create(size, mm::autoSpace());
        kind  = rt::GCFillKind::Struct;
    } else {
        empty = ::Tuple::create(size, mm::autoSpace());
        kind  = rt::GCFillKind::Tuple;
    }
    const gc_node_ref_t source =
        draft.materializeStaticValue(toSlot<camel::core::rtdata::Object *>(empty), type);
    return addFill(draft, type, kind, source, values);
}

/// A closure of `graph` (a graph key of the current group) capturing `captures`.
gc_node_ref_t addClosure(
    GraphDraft &draft, GCGraph *graph, FunctionType *type, TupleType *closureType,
    std::span<const gc_node_ref_t> captures) {
    auto *function             = ::Function::create(graph, closureType, mm::autoSpace());
    const gc_node_ref_t source = draft.materializeStaticValue(toSlot<::Function *>(function), type);
    return addFill(draft, type, rt::GCFillKind::FunctionClosure, source, captures);
}

gc_node_ref_t addPort(GraphDraft &draft, Type *type) {
    return draft.addPortNode(type, draft.allocateRuntimeSlot(type));
}

/// Makes `result` what the graph returns, keeping the effects the old exit waited for.
void setResult(GraphDraft &draft, gc_node_ref_t result) {
    const gc_node_ref_t oldExit   = draft.exitNode();
    const gc_node_ref_t oldOutput = draft.outputNode();
    gc_node_ref_t exit            = result;
    if (oldExit != kInvalidNodeRef && oldExit != oldOutput && oldExit != result) {
        exit = draft.addGateNode(typeOf(draft, result));
        draft.setNormInputs(exit, std::span<const gc_node_ref_t>(&result, 1));
        draft.setCtrlInputs(exit, std::span<const gc_node_ref_t>(&oldExit, 1));
    }
    draft.setOutputNode(exit);
    draft.setExitNode(exit);
    draft.setReturnNode(exit, rt::GCReturnKind::Self);
}

gc_node_ref_t valueNode(const GraphDraft &draft) {
    gc_node_ref_t output = draft.outputNode();
    if (output == kInvalidNodeRef) {
        output = draft.returnNode();
    }
    if (output == kInvalidNodeRef) {
        fail("function graph has no result");
    }
    return output;
}

/// Nodes the value of `root` depends on, inputs before users.
std::vector<gc_node_ref_t> valueOrder(const GraphDraft &draft, gc_node_ref_t root) {
    std::vector<gc_node_ref_t> order;
    std::unordered_set<gc_node_ref_t> seen;
    std::vector<std::pair<gc_node_ref_t, bool>> stack{{root, false}};
    while (!stack.empty()) {
        auto [node, expanded] = stack.back();
        stack.pop_back();
        if (expanded) {
            order.push_back(node);
            continue;
        }
        if (!seen.insert(node).second) {
            continue;
        }
        stack.emplace_back(node, true);
        for (gc_node_ref_t input : draft.withInputsOf(node)) {
            if (!seen.contains(input)) {
                stack.emplace_back(input, false);
            }
        }
        for (gc_node_ref_t input : draft.normInputsOf(node)) {
            if (!seen.contains(input)) {
                stack.emplace_back(input, false);
            }
        }
    }
    return order;
}

/// Erases the nodes the graph's result does not depend on (backward nodes computing gradients
/// of constants, forward values only the gradient needed not), keeping ports and closure nodes.
void pruneUnreachable(GraphDraft &draft) {
    std::vector<bool> live(draft.nodeSlotCount(), false);
    std::vector<gc_node_ref_t> stack;
    for (gc_node_ref_t root : {draft.exitNode(), draft.outputNode(), draft.returnNode()}) {
        if (root != kInvalidNodeRef) {
            stack.push_back(root);
        }
    }
    while (!stack.empty()) {
        const gc_node_ref_t node = stack.back();
        stack.pop_back();
        if (live[node]) {
            continue;
        }
        live[node] = true;
        for (auto inputs :
             {draft.normInputsOf(node), draft.withInputsOf(node), draft.ctrlInputsOf(node)}) {
            for (gc_node_ref_t input : inputs) {
                if (!live[input]) {
                    stack.push_back(input);
                }
            }
        }
    }
    std::unordered_set<gc_node_ref_t> kept(draft.withPorts().begin(), draft.withPorts().end());
    kept.insert(draft.normPorts().begin(), draft.normPorts().end());
    kept.insert(draft.closureNodes().begin(), draft.closureNodes().end());
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (draft.alive(id) && !live[id] && !kept.contains(id)) {
            draft.eraseNode(id);
        }
    }
}

std::vector<Type *> portTypesOf(FunctionType *type) {
    std::vector<Type *> types;
    for (size_t i = 0; i < type->withTypesCount(); ++i) {
        types.push_back(type->withTypeAt(i));
    }
    for (size_t i = 0; i < type->normTypesCount(); ++i) {
        types.push_back(type->normTypeAt(i));
    }
    return types;
}

std::vector<gc_node_ref_t> portsOf(const GraphDraft &draft) {
    std::vector<gc_node_ref_t> ports = copyOf(draft.withPorts());
    for (gc_node_ref_t port : draft.normPorts()) {
        ports.push_back(port);
    }
    return ports;
}

bool anyTangent(std::span<Type *const> types) {
    for (Type *t : types) {
        if (tangentTypeOf(t) != nullptr) {
            return true;
        }
    }
    return false;
}

std::string uniqueSuffix() {
    static std::atomic<size_t> next{0};
    return std::format("${}", next.fetch_add(1, std::memory_order_relaxed));
}

// ---------------------------------------------------------------- plans

/// The graphs differentiating one function: its forward graph returning (value, pullback) and
/// the pullback graph, with their types. Tangents are ordered like the function's ports (with
/// ports, then norm ports), skipping ports without a tangent.
struct PullbackPlan {
    GCGraph *forward  = nullptr; // group key
    GCGraph *pullback = nullptr; // group key
    std::vector<Type *> portTypes;
    Type *valueType             = nullptr;
    TupleType *tangentsType     = nullptr;
    FunctionType *pullbackType  = nullptr;
    TupleType *forwardValueType = nullptr;

    /// Position of port `index` in the tangents, or nullopt.
    std::optional<size_t> tangentIndex(size_t index) const {
        if (tangentTypeOf(portTypes[index]) == nullptr) {
            return std::nullopt;
        }
        size_t position = 0;
        for (size_t i = 0; i < index; ++i) {
            position += tangentTypeOf(portTypes[i]) != nullptr ? 1 : 0;
        }
        return position;
    }
};

PullbackPlan makePlanTypes(std::vector<Type *> portTypes, Type *valueType) {
    PullbackPlan plan;
    std::vector<Type *> tangents;
    for (Type *t : portTypes) {
        if (Type *tangent = tangentTypeOf(t)) {
            tangents.push_back(tangent);
        }
    }
    plan.portTypes    = std::move(portTypes);
    plan.valueType    = valueType;
    plan.tangentsType = TupleType::create(std::move(tangents));
    plan.pullbackType = FunctionType::create(
        param_vec_t{},
        param_vec_t{{tangentTypeOf(valueType), false}},
        plan.tangentsType);
    plan.forwardValueType = TupleType::create(std::vector<Type *>{valueType, plan.pullbackType});
    return plan;
}

/// A differentiable call in a forward graph: its value, the pullback it returned, and the
/// inputs the pullback's tangents belong to.
struct CallSite {
    gc_node_ref_t pullback = kInvalidNodeRef;
    std::vector<gc_node_ref_t> inputs;
    PullbackPlan plan;
    /// A call of a function carrying a custom rule (see rules.h): the rule replaces the pullback.
    ::Function *rule = nullptr;
};

class Engine;

// ---------------------------------------------------------------- forward rewrite

/// Rewrites a graph so that each differentiable call also returns its pullback.
class ForwardRewrite {
  public:
    ForwardRewrite(Engine &engine, GraphDraft &draft) : engine_(engine), draft_(draft) {}

    void run();

    /// Call sites keyed by the node carrying the call's value.
    const std::unordered_map<gc_node_ref_t, CallSite> &sites() const { return sites_; }

  private:
    void rewriteBranch(gc_node_ref_t join);
    void rewriteCall(gc_node_ref_t call);
    /// Calls of static function values: records custom-rule call sites, and turns calls of other
    /// capture-free functions into direct calls. Returns the direct call created, if any.
    std::optional<gc_node_ref_t> rewriteIndirectCall(gc_node_ref_t call);
    /// Makes `node` return `plan`'s (value, pullback) and splits the pair for its users.
    void split(gc_node_ref_t node, const PullbackPlan &plan, std::vector<gc_node_ref_t> inputs);
    void retarget(gc_node_ref_t func, const PullbackPlan &plan);

    Engine &engine_;
    GraphDraft &draft_;
    std::unordered_map<gc_node_ref_t, CallSite> sites_;
};

// ---------------------------------------------------------------- backward emission

/// Gradient accumulated for one value. Aggregates (structs, tuples) are kept element by element,
/// so reading one field of a model never materializes gradients for the others.
struct Tangent {
    gc_node_ref_t value = kInvalidNodeRef;
    std::map<size_t, Tangent> elements;
    gc_node_ref_t materialized = kInvalidNodeRef;

    bool empty() const {
        if (value != kInvalidNodeRef) {
            return false;
        }
        for (const auto &[index, element] : elements) {
            if (!element.empty()) {
                return false;
            }
        }
        return true;
    }
};

/// Emits the backward pass of a forward graph into a target graph: the forward graph itself
/// (grad of the top-level function), or a separate pullback graph that captures the primal
/// values it reads.
class Backward final : public VjpBuilder {
  public:
    Backward(
        GraphDraft &forward, GraphDraft &target,
        const std::unordered_map<gc_node_ref_t, CallSite> &sites)
        : forward_(forward), target_(target), sites_(sites), inPlace_(&forward == &target) {}

    /// Target-graph node holding the primal value `primal`.
    gc_node_ref_t ref(gc_node_ref_t primal);

    void seed(gc_node_ref_t primal, gc_node_ref_t gradient) { gradients_[primal].value = gradient; }
    /// Propagates gradients through the forward nodes `order` (inputs before users).
    void run(const std::vector<gc_node_ref_t> &order);
    /// Full gradient of a primal value; zero when none reached it.
    gc_node_ref_t gradient(gc_node_ref_t primal);

    /// Primal nodes captured by the target graph, in closure order, after dropping unused ones.
    std::vector<gc_node_ref_t> finishCaptures();

    // VjpBuilder
    Type *nodeType(vjp_node_t node) const override { return typeOf(target_, node); }
    std::optional<slot_t> staticValueOf(vjp_node_t node) const override {
        return staticValue(target_, node);
    }
    vjp_node_t addStatic(slot_t value, Type *type) override {
        return target_.materializeStaticValue(value, type);
    }
    vjp_node_t
    addOper(Type *type, std::string_view uri, std::span<const vjp_node_t> normInputs) override {
        return autodiff::addOper(target_, type, uri, normInputs);
    }
    std::optional<vjp_node_t> gradientOf(vjp_node_t node) const override;
    void accumulateGradient(vjp_node_t node, vjp_node_t gradient) override;

  private:
    std::optional<gc_node_ref_t> primalOf(gc_node_ref_t target) const;
    void accumulate(gc_node_ref_t primal, gc_node_ref_t gradient);
    void accumulateInto(Tangent &tangent, Type *primalType, gc_node_ref_t gradient);
    void mergeInto(Tangent &tangent, Type *primalType, Tangent &&other);
    gc_node_ref_t materialize(Tangent &tangent, Type *primalType, gc_node_ref_t primal);
    gc_node_ref_t zero(Type *primalType, gc_node_ref_t primal);
    gc_node_ref_t add(Type *primalType, gc_node_ref_t lhs, gc_node_ref_t rhs);

    void visit(gc_node_ref_t node);
    void visitCallSite(gc_node_ref_t node, const CallSite &site);
    void visitRuleSite(gc_node_ref_t node, const CallSite &site);
    void visitOper(gc_node_ref_t node);
    void visitAccs(gc_node_ref_t node);
    void visitFill(gc_node_ref_t node);
    void visitForward(gc_node_ref_t node, bool convert);

    GraphDraft &forward_;
    GraphDraft &target_;
    const std::unordered_map<gc_node_ref_t, CallSite> &sites_;
    const bool inPlace_;
    std::unordered_set<gc_node_ref_t> primals_;
    mutable std::unordered_map<gc_node_ref_t, Tangent> gradients_;
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> toTarget_;
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> toPrimal_;
    std::vector<gc_node_ref_t> captures_;
};

// ---------------------------------------------------------------- engine

class Engine {
  public:
    explicit Engine(const camel::core::context::context_ptr_t &context) : group_(context) {}

    GCGraph *gradient(GCGraph *function, bool withValue);

    /// The plan differentiating calls of `callee` (memoized; recursion reuses the plan).
    const PullbackPlan &planFor(GCGraph *callee);
    /// A plan for a synthesized graph (a branch arm) with the given types.
    PullbackPlan planForDraft(
        std::unique_ptr<GraphDraft> source, const std::string &name, const PullbackPlan &types);

  private:
    struct Pending {
        PullbackPlan plan;
        std::unique_ptr<GraphDraft> source;
    };

    PullbackPlan reserve(const std::string &name, const PullbackPlan &types);
    void build(Pending &pending);

    rt::GraphDraftGroup group_;
    std::unordered_map<GCGraph *, PullbackPlan> plans_;
    std::deque<Pending> pending_;
};

// ---------------------------------------------------------------- forward rewrite impl

void ForwardRewrite::run() {
    std::vector<gc_node_ref_t> joins;
    std::vector<gc_node_ref_t> calls;
    std::vector<gc_node_ref_t> indirect;
    std::unordered_set<gc_node_ref_t> arms;
    for (gc_node_ref_t id = 0; id < draft_.nodeSlotCount(); ++id) {
        if (!draft_.alive(id)) {
            continue;
        }
        switch (kindOf(draft_, id)) {
        case GCNodeKind::Join:
            joins.push_back(id);
            for (gc_node_ref_t arm : draft_.withInputsOf(id)) {
                arms.insert(arm);
            }
            break;
        case GCNodeKind::Func:
            calls.push_back(id);
            break;
        case GCNodeKind::Call:
            indirect.push_back(id);
            break;
        default:
            break;
        }
    }
    for (gc_node_ref_t call : indirect) {
        if (auto direct = rewriteIndirectCall(call)) {
            calls.push_back(*direct);
        }
    }
    for (gc_node_ref_t join : joins) {
        rewriteBranch(join);
    }
    for (gc_node_ref_t call : calls) {
        if (!arms.contains(call)) {
            rewriteCall(call);
        }
    }
}

void ForwardRewrite::retarget(gc_node_ref_t func, const PullbackPlan &plan) {
    auto payload                                                    = draft_.mutablePayloadOf(func);
    reinterpret_cast<rt::GCFuncBody *>(payload.data())->calleeGraph = plan.forward;
    draft_.setNodeDataType(func, plan.forwardValueType);
    draft_.setNodeDataIndex(func, draft_.allocateRuntimeSlot(plan.forwardValueType));
}

void ForwardRewrite::split(
    gc_node_ref_t node, const PullbackPlan &plan, std::vector<gc_node_ref_t> inputs) {
    const gc_node_ref_t value = draft_.addAccsNode(plan.valueType, 0u);
    draft_.replaceAllValueUses(node, value);
    // Control users (e.g. a later BRCH that must run after the values its arms capture) now wait
    // for the value, which itself waits for the call.
    // (Only the edges: the branch structure, e.g. BRCH -> JOIN, still names the JOIN itself.)
    for (gc_node_ref_t user : copyOf(draft_.ctrlUsersOf(node))) {
        draft_.replaceInput(rt::DraftEdgeKind::Ctrl, user, node, value);
    }
    if (draft_.outputNode() == node) {
        draft_.setOutputNode(value);
    }
    if (draft_.exitNode() == node) {
        draft_.setExitNode(value);
    }
    if (draft_.returnNode() == node) {
        draft_.setReturnNode(value, draft_.returnKind());
    }
    draft_.setNormInputs(value, std::span<const gc_node_ref_t>(&node, 1));
    const gc_node_ref_t pullback = addAccs(draft_, plan.pullbackType, node, 1);
    sites_.emplace(
        value,
        CallSite{.pullback = pullback, .inputs = std::move(inputs), .plan = plan});
}

std::optional<gc_node_ref_t> ForwardRewrite::rewriteIndirectCall(gc_node_ref_t call) {
    const std::vector<gc_node_ref_t> withInputs = copyOf(draft_.withInputsOf(call));
    if (withInputs.empty()) {
        return std::nullopt;
    }
    const auto calleeValue = staticValue(draft_, withInputs.front());
    auto *function         = calleeValue ? fromSlot<::Function *>(*calleeValue) : nullptr;
    if (function == nullptr || function->graph() == nullptr) {
        return std::nullopt; // a function computed at run time
    }
    GCGraph *graph = function->graph();
    std::vector<gc_node_ref_t> inputs(withInputs.begin() + 1, withInputs.end());
    for (gc_node_ref_t input : draft_.normInputsOf(call)) {
        inputs.push_back(input);
    }
    if (carriesRule(graph)) {
        FunctionType *type = graph->funcType();
        sites_.emplace(
            call,
            CallSite{
                .inputs = std::move(inputs),
                .plan   = makePlanTypes(portTypesOf(type), type->exitType()),
                .rule   = ruleOf(function),
            });
        return std::nullopt;
    }
    if (function->tupleType()->size() != 0) {
        return std::nullopt; // a closure: its graph cannot be called directly
    }
    // A static capture-free function: call its graph directly.
    const gc_node_ref_t direct = draft_.addFuncNode(graph, typeOf(draft_, call));
    draft_.setWithInputs(direct, std::span(withInputs).subspan(1));
    draft_.setNormInputs(direct, copyOf(draft_.normInputsOf(call)));
    draft_.setCtrlInputs(direct, copyOf(draft_.ctrlInputsOf(call)));
    draft_.replaceAllValueUses(call, direct);
    for (gc_node_ref_t user : copyOf(draft_.ctrlUsersOf(call))) {
        draft_.replaceInput(rt::DraftEdgeKind::Ctrl, user, call, direct);
    }
    if (draft_.outputNode() == call) {
        draft_.setOutputNode(direct);
    }
    if (draft_.exitNode() == call) {
        draft_.setExitNode(direct);
    }
    if (draft_.returnNode() == call) {
        draft_.setReturnNode(direct, draft_.returnKind());
    }
    draft_.eraseNode(call);
    return direct;
}

void ForwardRewrite::rewriteCall(gc_node_ref_t call) {
    GCGraph *callee = calleeOf(draft_, call);
    if (callee == nullptr) {
        fail("found a call without a callee graph");
    }
    FunctionType *type     = callee->funcType();
    const auto portTypes   = portTypesOf(type);
    Type *valueType        = type->exitType();
    const bool carriesFlow = tangentTypeOf(valueType) != nullptr && anyTangent(portTypes);
    if (!carriesFlow || type->modifiers().macro()) {
        return;
    }
    const PullbackPlan &plan          = engine_.planFor(callee);
    std::vector<gc_node_ref_t> inputs = copyOf(draft_.withInputsOf(call));
    for (gc_node_ref_t input : draft_.normInputsOf(call)) {
        inputs.push_back(input);
    }
    retarget(call, plan);
    split(call, plan, std::move(inputs));
}

void ForwardRewrite::rewriteBranch(gc_node_ref_t join) {
    Type *valueType = typeOf(draft_, join);
    if (tangentTypeOf(valueType) == nullptr) {
        return;
    }
    const std::vector<gc_node_ref_t> arms = copyOf(draft_.withInputsOf(join));

    // All arms take the union of the values any arm reads, so their pullbacks share one type.
    std::vector<gc_node_ref_t> shared;
    for (gc_node_ref_t arm : arms) {
        if (kindOf(draft_, arm) != GCNodeKind::Func) {
            fail("branch arms must be subgraph calls; differentiate before inlining them");
        }
        if (!draft_.normInputsOf(arm).empty()) {
            fail("branch arm with positional inputs is not supported");
        }
        for (gc_node_ref_t input : draft_.withInputsOf(arm)) {
            if (std::find(shared.begin(), shared.end(), input) == shared.end()) {
                shared.push_back(input);
            }
        }
    }
    std::vector<Type *> sharedTypes;
    for (gc_node_ref_t input : shared) {
        sharedTypes.push_back(typeOf(draft_, input));
    }
    const PullbackPlan types = makePlanTypes(sharedTypes, valueType);

    PullbackPlan joined;
    for (gc_node_ref_t arm : arms) {
        GCGraph *armGraph                          = calleeOf(draft_, arm);
        auto variant                               = GraphDraft::decode(armGraph);
        const std::vector<gc_node_ref_t> oldPorts  = copyOf(variant->withPorts());
        const std::vector<gc_node_ref_t> oldInputs = copyOf(draft_.withInputsOf(arm));
        for (gc_node_ref_t port : oldPorts) {
            variant->removeWithPort(port);
        }
        param_vec_t withParams;
        for (size_t i = 0; i < shared.size(); ++i) {
            auto it                  = std::find(oldInputs.begin(), oldInputs.end(), shared[i]);
            const gc_node_ref_t port = it != oldInputs.end()
                                           ? oldPorts[static_cast<size_t>(it - oldInputs.begin())]
                                           : addPort(*variant, sharedTypes[i]);
            variant->appendWithPort(port);
            withParams.emplace_back(sharedTypes[i], false);
        }
        FunctionType *armType = armGraph->funcType();
        param_vec_t normParams;
        for (size_t i = 0; i < armType->normTypesCount(); ++i) {
            normParams.emplace_back(armType->normTypeAt(i), false);
        }
        variant->setFuncType(
            FunctionType::create(withParams, normParams, valueType, armType->modifiers()));

        joined = engine_.planForDraft(std::move(variant), armGraph->name(), types);
        retarget(arm, joined);
        draft_.setWithInputs(arm, shared);
    }
    draft_.setNodeDataType(join, types.forwardValueType);
    draft_.setNodeDataIndex(join, draft_.allocateRuntimeSlot(types.forwardValueType));
    split(join, joined, shared);
}

// ---------------------------------------------------------------- backward impl

gc_node_ref_t Backward::ref(gc_node_ref_t primal) {
    if (inPlace_) {
        return primal;
    }
    if (auto it = toTarget_.find(primal); it != toTarget_.end()) {
        return it->second;
    }
    gc_node_ref_t node;
    if (auto value = staticValue(forward_, primal)) {
        // Constants are copied rather than captured.
        node = target_.materializeStaticValue(*value, typeOf(forward_, primal));
    } else {
        node = addPort(target_, typeOf(forward_, primal));
        target_.appendClosureNode(node);
        captures_.push_back(primal);
    }
    toTarget_.emplace(primal, node);
    toPrimal_.emplace(node, primal);
    return node;
}

std::optional<gc_node_ref_t> Backward::primalOf(gc_node_ref_t target) const {
    if (inPlace_) {
        return primals_.contains(target) ? std::optional(target) : std::nullopt;
    }
    if (auto it = toPrimal_.find(target); it != toPrimal_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<vjp_node_t> Backward::gradientOf(vjp_node_t node) const {
    const auto primal = primalOf(node);
    if (!primal) {
        return std::nullopt;
    }
    auto it = gradients_.find(*primal);
    if (it == gradients_.end() || it->second.empty()) {
        return std::nullopt;
    }
    return const_cast<Backward *>(this)->gradient(*primal);
}

void Backward::accumulateGradient(vjp_node_t node, vjp_node_t gradient) {
    if (const auto primal = primalOf(node)) {
        accumulate(*primal, gradient);
    }
}

void Backward::accumulate(gc_node_ref_t primal, gc_node_ref_t gradient) {
    Type *type = typeOf(forward_, primal);
    if (tangentTypeOf(type) == nullptr || staticValue(forward_, primal)) {
        return; // no tangent, or a constant
    }
    accumulateInto(gradients_[primal], type, gradient);
}

void Backward::accumulateInto(Tangent &tangent, Type *primalType, gc_node_ref_t gradient) {
    ASSERT(tangent.materialized == kInvalidNodeRef, "autodiff: gradient changed after use.");
    if (isAggregate(primalType)) {
        // Keep aggregates element-wise: split a whole-value gradient into its elements.
        Type *tangentType = tangentTypeOf(primalType);
        for (size_t i = 0; i < aggregateSize(primalType); ++i) {
            const auto position = tangentElementIndex(primalType, i);
            if (!position) {
                continue;
            }
            const gc_node_ref_t element = addField(target_, tangentType, gradient, *position);
            accumulateInto(tangent.elements[i], aggregateElement(primalType, i), element);
        }
        return;
    }
    tangent.value =
        tangent.value == kInvalidNodeRef ? gradient : add(primalType, tangent.value, gradient);
}

void Backward::mergeInto(Tangent &tangent, Type *primalType, Tangent &&other) {
    if (other.value != kInvalidNodeRef) {
        accumulateInto(tangent, primalType, other.value);
    }
    for (auto &[index, element] : other.elements) {
        mergeInto(tangent.elements[index], aggregateElement(primalType, index), std::move(element));
    }
}

gc_node_ref_t Backward::add(Type *primalType, gc_node_ref_t lhs, gc_node_ref_t rhs) {
    const TangentSpace *space = DerivativeRegistry::instance().findTangentSpace(primalType);
    if (space == nullptr || space->addUri.empty()) {
        fail(std::format("cannot add gradients of type '{}'", primalType->toString()));
    }
    const std::array<gc_node_ref_t, 2> inputs{lhs, rhs};
    return autodiff::addOper(target_, tangentTypeOf(primalType), space->addUri, inputs);
}

gc_node_ref_t Backward::zero(Type *primalType, gc_node_ref_t primal) {
    if (isAggregate(primalType)) {
        Tangent none;
        return materialize(none, primalType, primal);
    }
    const TangentSpace *space = DerivativeRegistry::instance().findTangentSpace(primalType);
    if (space == nullptr) {
        fail(std::format("no zero gradient for type '{}'", primalType->toString()));
    }
    if (space->zero) {
        return target_.materializeStaticValue(*space->zero, tangentTypeOf(primalType));
    }
    const gc_node_ref_t value = primal;
    return autodiff::addOper(
        target_,
        tangentTypeOf(primalType),
        space->zerosLikeUri,
        std::span<const gc_node_ref_t>(&value, 1));
}

// `primal` is the target-graph node of the primal value, used for zeros only.
gc_node_ref_t Backward::materialize(Tangent &tangent, Type *primalType, gc_node_ref_t primal) {
    if (tangent.materialized != kInvalidNodeRef) {
        return tangent.materialized;
    }
    if (!isAggregate(primalType)) {
        tangent.materialized =
            tangent.value != kInvalidNodeRef ? tangent.value : zero(primalType, primal);
        return tangent.materialized;
    }
    std::vector<gc_node_ref_t> values;
    for (size_t i = 0; i < aggregateSize(primalType); ++i) {
        if (!tangentElementIndex(primalType, i)) {
            continue;
        }
        Type *elementType = aggregateElement(primalType, i);
        auto it           = tangent.elements.find(i);
        const bool needsPrimal =
            it == tangent.elements.end() || it->second.empty() || isAggregate(elementType);
        const gc_node_ref_t element =
            needsPrimal ? addField(target_, primalType, primal, i) : kInvalidNodeRef;
        if (it == tangent.elements.end()) {
            values.push_back(zero(elementType, element));
        } else {
            values.push_back(materialize(it->second, elementType, element));
        }
    }
    tangent.materialized = addAggregate(target_, tangentTypeOf(primalType), values);
    return tangent.materialized;
}

gc_node_ref_t Backward::gradient(gc_node_ref_t primal) {
    Type *type       = typeOf(forward_, primal);
    Tangent &tangent = gradients_[primal];
    if (tangent.materialized != kInvalidNodeRef) {
        return tangent.materialized;
    }
    // The primal value is only read for zeros; avoid capturing it otherwise.
    const bool needsPrimal = tangent.empty() || isAggregate(type);
    return materialize(tangent, type, needsPrimal ? ref(primal) : kInvalidNodeRef);
}

void Backward::run(const std::vector<gc_node_ref_t> &order) {
    primals_.insert(order.begin(), order.end());
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        auto found = gradients_.find(*it);
        if (found != gradients_.end() && !found->second.empty()) {
            visit(*it);
        }
    }
}

void Backward::visit(gc_node_ref_t node) {
    if (auto site = sites_.find(node); site != sites_.end()) {
        visitCallSite(node, site->second);
        return;
    }
    switch (kindOf(forward_, node)) {
    case GCNodeKind::Oper:
        visitOper(node);
        return;
    case GCNodeKind::Accs:
        visitAccs(node);
        return;
    case GCNodeKind::Fill:
        visitFill(node);
        return;
    case GCNodeKind::Cast:
        visitForward(node, true);
        return;
    case GCNodeKind::Copy:
    case GCNodeKind::Gate:
        visitForward(node, false);
        return;
    case GCNodeKind::Data:
    case GCNodeKind::Port:
    case GCNodeKind::Brch:
    case GCNodeKind::Sync:
        return;
    case GCNodeKind::Func:
        // Calls the forward rewrite left alone take no differentiable input: constants.
        return;
    case GCNodeKind::Call:
        fail("cannot differentiate through a call of a function value; the callee must be known "
             "statically");
    default:
        fail(std::format(
            "cannot differentiate through node kind {}",
            static_cast<int>(kindOf(forward_, node))));
    }
}

void Backward::visitCallSite(gc_node_ref_t node, const CallSite &site) {
    if (site.rule != nullptr) {
        visitRuleSite(node, site);
        return;
    }
    const gc_node_ref_t dy       = gradient(node);
    const gc_node_ref_t pullback = ref(site.pullback);
    const gc_node_ref_t tangents =
        addCall(target_, site.plan.tangentsType, pullback, std::span<const gc_node_ref_t>(&dy, 1));
    for (size_t i = 0; i < site.inputs.size(); ++i) {
        if (const auto position = site.plan.tangentIndex(i)) {
            accumulate(
                site.inputs[i],
                addAccs(target_, tangentTypeOf(site.plan.portTypes[i]), tangents, *position));
        }
    }
}

// rule(inputs..., dy) returns the gradient of the one differentiable input, or a tuple of the
// gradients of all of them.
void Backward::visitRuleSite(gc_node_ref_t node, const CallSite &site) {
    FunctionType *ruleType = site.rule->graph()->funcType();
    std::vector<gc_node_ref_t> args;
    for (gc_node_ref_t input : site.inputs) {
        args.push_back(ref(input));
    }
    args.push_back(gradient(node));
    const gc_node_ref_t rule =
        target_.materializeStaticValue(toSlot<::Function *>(site.rule), ruleType);
    const gc_node_ref_t result = addCall(target_, ruleType->exitType(), rule, args);
    const size_t count         = site.plan.tangentsType->size();
    for (size_t i = 0; i < site.inputs.size(); ++i) {
        if (const auto position = site.plan.tangentIndex(i)) {
            accumulate(
                site.inputs[i],
                count == 1
                    ? result
                    : addAccs(target_, tangentTypeOf(site.plan.portTypes[i]), result, *position));
        }
    }
}

void Backward::visitOper(gc_node_ref_t node) {
    const auto payload = forward_.payloadOf(node);
    const auto *body   = reinterpret_cast<const rt::GCOperBody *>(payload.data());
    const std::string uri(body->uri());
    // Operators none of whose inputs can take a gradient (conversions from integers,
    // constructors from shapes) need no rule.
    bool differentiable = false;
    for (gc_node_ref_t input : forward_.normInputsOf(node)) {
        differentiable |=
            tangentTypeOf(typeOf(forward_, input)) != nullptr && !staticValue(forward_, input);
    }
    if (!differentiable) {
        return;
    }
    const camel::core::VjpRule rule = DerivativeRegistry::instance().findRule(uri);
    if (rule == nullptr) {
        fail(std::format("no derivative rule for operator '{}'", uri));
    }
    std::vector<gc_node_ref_t> inputs;
    for (gc_node_ref_t input : forward_.normInputsOf(node)) {
        inputs.push_back(ref(input));
    }
    rule(*this, VjpCall{.uri = uri, .inputs = inputs, .output = ref(node)});
}

void Backward::visitAccs(gc_node_ref_t node) {
    const gc_node_ref_t source = forward_.normInputsOf(node).front();
    Type *sourceType           = typeOf(forward_, source);
    if (!isAggregate(sourceType) || staticValue(forward_, source)) {
        return;
    }
    const auto payload = forward_.payloadOf(node);
    const auto *body   = reinterpret_cast<const rt::GCAccsBody *>(payload.data());
    size_t index       = body->value;
    if (body->accsKind == rt::GCAccsKind::StructKey) {
        const auto field = tt::as_ptr<StructType>(sourceType)->findField(body->key());
        ASSERT(field.has_value(), "autodiff: struct field lookup failed.");
        index = *field;
    }
    Tangent moved = std::move(gradients_[node]);
    gradients_.erase(node);
    mergeInto(
        gradients_[source].elements[index],
        aggregateElement(sourceType, index),
        std::move(moved));
}

void Backward::visitFill(gc_node_ref_t node) {
    const auto payload = forward_.payloadOf(node);
    const auto *body   = reinterpret_cast<const rt::GCFillBody *>(payload.data());
    Type *type         = typeOf(forward_, node);
    if (body->fillKind != rt::GCFillKind::Tuple && body->fillKind != rt::GCFillKind::Struct) {
        fail("cannot differentiate through an array literal yet");
    }
    Tangent moved     = std::move(gradients_[node]);
    const auto values = copyOf(forward_.withInputsOf(node));
    const auto slots  = body->slots();
    for (size_t i = 0; i < values.size(); ++i) {
        const auto slot = static_cast<size_t>(slots[i]);
        auto element    = moved.elements.find(slot);
        if (element == moved.elements.end() || element->second.empty()) {
            continue;
        }
        Type *elementType = aggregateElement(type, slot);
        if (tangentTypeOf(typeOf(forward_, values[i])) == nullptr ||
            staticValue(forward_, values[i])) {
            continue;
        }
        mergeInto(gradients_[values[i]], elementType, std::move(element->second));
    }
}

void Backward::visitForward(gc_node_ref_t node, bool convert) {
    const auto inputs = forward_.normInputsOf(node);
    if (inputs.empty()) {
        return;
    }
    const gc_node_ref_t source = inputs.front();
    Type *sourceType           = typeOf(forward_, source);
    Type *sourceTangent        = tangentTypeOf(sourceType);
    if (sourceTangent == nullptr) {
        return;
    }
    gc_node_ref_t g = gradient(node);
    if (convert && !sourceTangent->equals(tangentTypeOf(typeOf(forward_, node)))) {
        const gc_node_ref_t cast = target_.addCastNode(sourceTangent);
        target_.setNormInputs(cast, std::span<const gc_node_ref_t>(&g, 1));
        g = cast;
    }
    accumulate(source, g);
}

std::vector<gc_node_ref_t> Backward::finishCaptures() {
    std::vector<gc_node_ref_t> kept;
    for (gc_node_ref_t primal : captures_) {
        const gc_node_ref_t node = toTarget_.at(primal);
        const bool used          = !target_.normUsersOf(node).empty() ||
                          !target_.withUsersOf(node).empty() || target_.outputNode() == node;
        if (used) {
            kept.push_back(primal);
        } else {
            target_.removeClosureNode(node);
            target_.eraseNode(node);
        }
    }
    return kept;
}

// ---------------------------------------------------------------- engine impl

PullbackPlan Engine::reserve(const std::string &name, const PullbackPlan &types) {
    PullbackPlan plan = types;
    const auto suffix = uniqueSuffix();
    plan.forward      = group_.reserve(
        "autodiff.fwd/" + name + suffix,
        "autodiff.fwd/" + name + suffix,
        name + ".fwd");
    plan.pullback = group_.reserve(
        "autodiff.pb/" + name + suffix,
        "autodiff.pb/" + name + suffix,
        name + ".pb");
    return plan;
}

const PullbackPlan &Engine::planFor(GCGraph *callee) {
    if (auto it = plans_.find(callee); it != plans_.end()) {
        return it->second;
    }
    FunctionType *type = callee->funcType();
    PullbackPlan plan = reserve(callee->name(), makePlanTypes(portTypesOf(type), type->exitType()));
    pending_.push_back(Pending{.plan = plan, .source = GraphDraft::decode(callee)});
    return plans_.emplace(callee, plan).first->second;
}

PullbackPlan Engine::planForDraft(
    std::unique_ptr<GraphDraft> source, const std::string &name, const PullbackPlan &types) {
    PullbackPlan plan = reserve(name, types);
    pending_.push_back(Pending{.plan = plan, .source = std::move(source)});
    return plan;
}

void Engine::build(Pending &pending) {
    const PullbackPlan &plan = pending.plan;
    GraphDraft &forward      = *pending.source;

    ForwardRewrite rewrite(*this, forward);
    rewrite.run();
    const gc_node_ref_t value = valueNode(forward);

    auto pullback = std::make_unique<GraphDraft>();
    pullback->setFuncType(plan.pullbackType);
    const gc_node_ref_t dy = addPort(*pullback, tangentTypeOf(plan.valueType));
    pullback->appendNormPort(dy);

    Backward backward(forward, *pullback, rewrite.sites());
    backward.seed(value, dy);
    backward.run(valueOrder(forward, value));

    const auto ports = portsOf(forward);
    std::vector<gc_node_ref_t> tangents;
    for (size_t i = 0; i < ports.size(); ++i) {
        if (plan.tangentIndex(i)) {
            tangents.push_back(backward.gradient(ports[i]));
        }
    }
    setResult(*pullback, addAggregate(*pullback, plan.tangentsType, tangents));
    pruneUnreachable(*pullback);

    const std::vector<gc_node_ref_t> captures = backward.finishCaptures();
    std::vector<Type *> captureTypes;
    for (gc_node_ref_t capture : captures) {
        captureTypes.push_back(typeOf(forward, capture));
    }
    TupleType *closureType = TupleType::create(std::move(captureTypes));
    pullback->setClosureType(closureType);

    const gc_node_ref_t closure =
        addClosure(forward, plan.pullback, plan.pullbackType, closureType, captures);
    const std::array<gc_node_ref_t, 2> pair{value, closure};
    setResult(forward, addAggregate(forward, plan.forwardValueType, pair));
    pruneUnreachable(forward);

    FunctionType *sourceType = forward.funcType();
    param_vec_t withParams, normParams;
    for (size_t i = 0; i < sourceType->withTypesCount(); ++i) {
        withParams.emplace_back(sourceType->withTypeAt(i), sourceType->withIsVarAt(i));
    }
    for (size_t i = 0; i < sourceType->normTypesCount(); ++i) {
        normParams.emplace_back(sourceType->normTypeAt(i), sourceType->normIsVarAt(i));
    }
    forward.setFuncType(FunctionType::create(
        withParams,
        normParams,
        plan.forwardValueType,
        sourceType->modifiers()));

    group_.define(plan.forward, std::move(pending.source));
    group_.define(plan.pullback, std::move(pullback));
}

GCGraph *Engine::gradient(GCGraph *function, bool withValue) {
    FunctionType *type   = function->funcType();
    const auto signature = gradientSignature(type, withValue);
    if (!signature) {
        fail(std::format(
            "cannot differentiate '{}' of type {}: it must return a float and take a "
            "differentiable parameter",
            function->name(),
            type->toString()));
    }

    auto draft = GraphDraft::decode(function);
    ForwardRewrite rewrite(*this, *draft);
    rewrite.run();
    const gc_node_ref_t value              = valueNode(*draft);
    const std::vector<gc_node_ref_t> order = valueOrder(*draft, value);

    Backward backward(*draft, *draft, rewrite.sites());
    const slot_t one = type->exitType()->code() == TypeCode::Float32
                           ? toSlot<camel::core::rtdata::Float32>(1.0f)
                           : toSlot<camel::core::rtdata::Float64>(1.0);
    backward.seed(value, draft->materializeStaticValue(one, type->exitType()));
    backward.run(order);

    std::vector<gc_node_ref_t> gradients;
    for (size_t index : signature->withPorts) {
        gradients.push_back(backward.gradient(draft->withPorts()[index]));
    }
    for (size_t index : signature->normPorts) {
        gradients.push_back(backward.gradient(draft->normPorts()[index]));
    }
    gc_node_ref_t result = gradients.size() == 1
                               ? gradients.front()
                               : addAggregate(*draft, signature->gradientType, gradients);
    if (withValue) {
        const std::array<gc_node_ref_t, 2> pair{value, result};
        result = addAggregate(*draft, signature->functionType->exitType(), pair);
    }
    setResult(*draft, result);
    pruneUnreachable(*draft);
    draft->setFuncType(signature->functionType);

    const std::string suffix = uniqueSuffix();
    GCGraph *key             = group_.reserve(
        "autodiff.grad/" + function->stableId() + suffix,
        "autodiff.grad/" + function->mangledName() + suffix,
        "grad(" + function->name() + ")");
    group_.define(key, std::move(draft));
    while (!pending_.empty()) {
        Pending pending = std::move(pending_.front());
        pending_.pop_front();
        build(pending);
    }
    group_.encode();
    return group_.encoded(key);
}

} // namespace

std::optional<GradientSignature> gradientSignature(FunctionType *function, bool withValue) {
    Type *valueType = function->exitType();
    if (valueType == nullptr ||
        (valueType->code() != TypeCode::Float64 && valueType->code() != TypeCode::Float32)) {
        return std::nullopt;
    }
    GradientSignature signature;
    std::vector<Type *> tangents;
    for (size_t i = 0; i < function->withTypesCount(); ++i) {
        if (Type *tangent = tangentTypeOf(function->withTypeAt(i))) {
            signature.withPorts.push_back(i);
            tangents.push_back(tangent);
        }
    }
    if (signature.withPorts.empty()) {
        for (size_t i = 0; i < function->normTypesCount(); ++i) {
            if (Type *tangent = tangentTypeOf(function->normTypeAt(i))) {
                signature.normPorts.push_back(i);
                tangents.push_back(tangent);
            }
        }
    }
    if (tangents.empty()) {
        return std::nullopt;
    }
    signature.gradientType =
        tangents.size() == 1 ? tangents.front() : TupleType::create(std::move(tangents));
    Type *result = withValue
                       ? TupleType::create(std::vector<Type *>{valueType, signature.gradientType})
                       : signature.gradientType;
    param_vec_t withParams, normParams;
    for (size_t i = 0; i < function->withTypesCount(); ++i) {
        withParams.emplace_back(function->withTypeAt(i), false);
    }
    for (size_t i = 0; i < function->normTypesCount(); ++i) {
        normParams.emplace_back(function->normTypeAt(i), false);
    }
    signature.functionType = FunctionType::create(withParams, normParams, result);
    return signature;
}

GCGraph *buildGradientGraph(
    const camel::core::context::context_ptr_t &context, GCGraph *function, bool withValue) {
    Engine engine(context);
    return engine.gradient(function, withValue);
}

} // namespace camel::autodiff
