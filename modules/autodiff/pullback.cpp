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
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Pullback transform (see pullback.h).
 */

#include "pullback.h"

#include "camel/passes/opt/inline.h"
#include "camel/runtime/arm_raise.h"

#include "rules.h"

#include "camel/core/context/context.h"
#include "camel/core/derivative.h"
#include "camel/core/error/runtime.h"
#include "camel/core/mm.h"
#include "camel/core/mm/root_handle.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/struct.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/source/manager.h"
#include "camel/core/type/collection.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/enum.h"
#include "camel/passes/opt/inline.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/loop.h"
#include "camel/runtime/loop_outline.h"
#include "camel/utils/type.h"

#include <algorithm>
#include <atomic>
#include <deque>
#include <format>
#include <functional>
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
using rt::LoopDraftView;
using type::ArrayType;
using type::FunctionType;
using type::FunctionTypeFactory;
using type::param_vec_t;
using type::StructType;
using type::TupleType;
using type::Type;
using type::TypeCode;

[[noreturn]] void fail(const std::string &message) {
    throwRuntimeFault(RuntimeDiag::RuntimeError, "autodiff: " + message);
}

/// A refusal that belongs to one place of the program: the diagnostic is reported there, not at
/// the grad call that happened to run the transformation.
[[noreturn]] void failAt(uint64_t origin, const std::string &message) {
    throw camel::core::error::RuntimeFault(
        RuntimeDiag::RuntimeError,
        {"autodiff: " + message},
        "",
        nullptr,
        camel::core::error::ExecutionSiteHint{
            .cachedOrigin = static_cast<camel::source::origin_id_t>(origin)});
}

/// The source of the program being differentiated, to say where a refused node is.
thread_local camel::source::source_context_ptr_t currentSource;

/// " (at file:line:column)" for a node origin, or nothing when it is unknown.
std::string whereIs(uint64_t origin) {
    if (!currentSource || origin == camel::source::kInvalidOriginId) {
        return {};
    }
    const auto *file = currentSource->fileForOrigin(origin);
    if (!file) {
        return {};
    }
    const auto range = currentSource->resolveOrigin(origin);
    return std::format(
        " (at {}:{}:{})",
        file->path,
        range.start.line + 1,
        range.start.character + 1);
}

/// A variable (`var`) is a cell; the graph passes the cell, not its content.
bool isCellType(Type *type) {
    return type != nullptr && type->code() == TypeCode::Tuple &&
           tt::as_ptr<TupleType>(type)->isCell();
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

bool isFunctionType(Type *type) { return type != nullptr && type->code() == TypeCode::Function; }

/// Whether a value of `type` is, or holds, a function value (a closure, a tuple of closures).
bool holdsFunction(Type *type) {
    if (isFunctionType(type)) {
        return true;
    }
    if (type == nullptr || isCellType(type) || !isAggregate(type)) {
        return false;
    }
    for (size_t i = 0; i < aggregateSize(type); ++i) {
        if (holdsFunction(aggregateElement(type, i))) {
            return true;
        }
    }
    return false;
}

/// The value a chain of gates passes on. A gate orders a value after other nodes (an inlined
/// call gates its arguments after what the call waited for); it does not change the value.
gc_node_ref_t ungated(const GraphDraft &draft, gc_node_ref_t node) {
    while (kindOf(draft, node) == GCNodeKind::Gate && !draft.normInputsOf(node).empty()) {
        node = draft.normInputsOf(node).back();
    }
    return node;
}

/// What `node` and the gates it passes through wait for, the gated value's own ordering included.
std::vector<gc_node_ref_t> orderingOf(const GraphDraft &draft, gc_node_ref_t node) {
    std::vector<gc_node_ref_t> ctrl;
    while (true) {
        for (gc_node_ref_t before : draft.ctrlInputsOf(node)) {
            if (std::find(ctrl.begin(), ctrl.end(), before) == ctrl.end()) {
                ctrl.push_back(before);
            }
        }
        if (kindOf(draft, node) != GCNodeKind::Gate || draft.normInputsOf(node).empty()) {
            return ctrl;
        }
        node = draft.normInputsOf(node).back();
    }
}

/// The function a function value is, where the graph itself says so: a constant function, or a
/// closure built in this graph (FILL) over a constant function. Null for anything else (a
/// port, the result of a branch or of a call, an element of a container).
::Function *evidentFunction(const GraphDraft &draft, gc_node_ref_t node) {
    node = ungated(draft, node);
    if (!isFunctionType(typeOf(draft, node))) {
        return nullptr;
    }
    gc_node_ref_t constant = node;
    if (kindOf(draft, node) == GCNodeKind::Fill) {
        const auto *body = reinterpret_cast<const rt::GCFillBody *>(draft.payloadOf(node).data());
        if (body->fillKind != rt::GCFillKind::FunctionClosure ||
            draft.normInputsOf(node).size() != 1) {
            return nullptr;
        }
        constant = draft.normInputsOf(node).front();
        if (!isFunctionType(typeOf(draft, constant))) {
            return nullptr;
        }
    }
    const auto value = staticValue(draft, constant);
    auto *function   = value ? fromSlot<::Function *>(*value) : nullptr;
    return function != nullptr && function->graph() != nullptr ? function : nullptr;
}

GCGraph *calleeOf(const GraphDraft &draft, gc_node_ref_t node) {
    return draft.calleeGraphOf(node);
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

/// A closure of `graph` (a graph of the current group) capturing `captures`.
gc_node_ref_t addClosure(
    rt::GraphDraftGroup &group, GraphDraft &draft, rt::GraphId graph, FunctionType *type,
    TupleType *closureType, std::span<const gc_node_ref_t> captures) {
    auto *function = ::Function::create(group.program(), graph, closureType, mm::autoSpace());
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

/// Whether anything `node` runs after is ordered by a control edge (code with effects).
bool hasOrdering(const GraphDraft &draft, gc_node_ref_t node) {
    std::unordered_set<gc_node_ref_t> seen{node};
    std::vector<gc_node_ref_t> work{node};
    while (!work.empty()) {
        const gc_node_ref_t current = work.back();
        work.pop_back();
        if (!draft.ctrlInputsOf(current).empty()) {
            return true;
        }
        for (auto inputs : {draft.normInputsOf(current), draft.withInputsOf(current)}) {
            for (gc_node_ref_t input : inputs) {
                if (seen.insert(input).second) {
                    work.push_back(input);
                }
            }
        }
    }
    return false;
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

/// The types of the ports of `graph` (with ports, then norm ports), read from the port nodes.
/// The function type of a graph does not always describe its ports: a branch arm takes what it
/// reads as with ports under the type `() => T`, and a copy specialized to constant arguments
/// keeps the type of the function it was copied from although the bound parameters are gone.
/// Reading tangent positions off such a type drops gradients without any error.
std::vector<Type *> portTypesOf(const GCGraph *graph) {
    std::vector<Type *> types;
    for (auto ports : {graph->withPorts(), graph->normPorts()}) {
        for (gc_node_ref_t port : ports) {
            types.push_back(graph->node(port)->dataType());
        }
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

/// Which ports a plan makes room for: a gradient of an aggregate or array of such values, or of a
/// type the derivative registry describes. An array's tangent is the array of its elements'
/// tangents (design principles 9.9), so a loop carries an array's gradient in a tangent array; an
/// array whose elements have no tangent has none, and a loop over or capturing one is refused
/// instead (see LoopArray below).
bool carriesGradient(Type *type) {
    if (type == nullptr || tangentTypeOf(type) == nullptr) {
        return false;
    }
    if (type->code() == TypeCode::Array) {
        return carriesGradient(tt::as_ptr<type::ArrayType>(type)->elemType());
    }
    if (isAggregate(type)) {
        for (size_t i = 0; i < aggregateSize(type); ++i) {
            if (tangentElementIndex(type, i) && carriesGradient(aggregateElement(type, i))) {
                return true;
            }
        }
        return false;
    }
    return DerivativeRegistry::instance().findTangentSpace(type) != nullptr;
}

/// The rule a plan follows for "this port gets a gradient": every type that has a tangent, except
/// in a loop, where only the types a gradient can be carried in do (carriesGradient).
using TangentEligibility = bool (*)(Type *);

bool hasTangentType(Type *type) { return tangentTypeOf(type) != nullptr; }

/// The graphs differentiating one function: its forward graph returning (value, pullback) and
/// the pullback graph, with their types. Tangents are ordered like the function's ports (with
/// ports, then norm ports), skipping ports without a tangent.
struct PullbackPlan {
    rt::GraphId forward  = rt::kInvalidGraphId; // a graph of the group
    rt::GraphId pullback = rt::kInvalidGraphId; // a graph of the group
    std::vector<Type *> portTypes;
    Type *valueType             = nullptr;
    TupleType *tangentsType     = nullptr;
    FunctionType *pullbackType  = nullptr;
    TupleType *forwardValueType = nullptr;
    TangentEligibility eligible = hasTangentType;

    /// Position of port `index` in the tangents, or nullopt.
    std::optional<size_t> tangentIndex(size_t index) const {
        if (!eligible(portTypes[index])) {
            return std::nullopt;
        }
        size_t position = 0;
        for (size_t i = 0; i < index; ++i) {
            position += eligible(portTypes[i]) ? 1 : 0;
        }
        return position;
    }
};

PullbackPlan makePlanTypes(
    std::vector<Type *> portTypes, Type *valueType, TangentEligibility eligible = hasTangentType) {
    PullbackPlan plan;
    std::vector<Type *> tangents;
    for (Type *t : portTypes) {
        if (eligible(t)) {
            tangents.push_back(tangentTypeOf(t));
        }
    }
    Type *const dyType = eligible(valueType) ? tangentTypeOf(valueType) : nullptr;
    plan.portTypes     = std::move(portTypes);
    plan.valueType     = valueType;
    plan.eligible      = eligible;
    plan.tangentsType  = TupleType::create(std::move(tangents));
    plan.pullbackType =
        FunctionType::create(param_vec_t{}, param_vec_t{{dyType, false}}, plan.tangentsType);
    plan.forwardValueType = TupleType::create(std::vector<Type *>{valueType, plan.pullbackType});
    return plan;
}

/// An array a differentiated loop reads: the array it iterates over, or one it captures. A
/// captured array now carries a gradient (its tangent is the array of its elements' tangents),
/// so only an iterated array is refused: the reverse pass does not scatter each round's element
/// gradient back into it (design principles 9.9). The iterator is recorded so the refusal can
/// name it where it is written.
struct LoopArray {
    gc_node_ref_t array = kInvalidNodeRef;
    bool iterated       = false;
};

/// A LOOP node the forward rewrite gave collection slots to (the tape, design principles 9.9):
/// the node, for the located refusal, and the arrays the loop reads.
struct LoopSite {
    gc_node_ref_t node = kInvalidNodeRef;
    std::vector<LoopArray> arrays;
};

/// A differentiable call in a forward graph: its value, the pullback it returned, and the
/// inputs the pullback's tangents belong to.
struct CallSite {
    gc_node_ref_t pullback = kInvalidNodeRef;
    std::vector<gc_node_ref_t> inputs;
    PullbackPlan plan;
    /// A call of a function carrying a custom rule (see rules.h): the rule replaces the pullback.
    ::Function *rule = nullptr;
    /// Set when the call is a differentiated loop: the node reported if the loop reads an array
    /// that depends on a differentiated parameter.
    const LoopSite *loop = nullptr;
};

/// What a graph being differentiated is to the differentiated function: the reason a function
/// value is not evident in it depends on that.
enum class GraphRole { Differentiated, Callee, BranchArm, LoopBody };

/// Where a graph being differentiated stands.
struct GraphScope {
    GraphRole role = GraphRole::Differentiated;
    /// The functions this graph is inside a branch arm of, after their bodies were brought
    /// into a graph to follow a function value. A call of one of them from here is that
    /// function's recursion: it is not followed again (it would be without end), it stays a
    /// call.
    std::unordered_set<GCGraph *> enclosing;
    /// Set when this graph is (an arm of) a function whose recursive call was left that way.
    std::string recursive;
};

class Engine;

/// The two graphs a differentiated loop body needs (design principles 9.9):
///
///   body.fwd  : the body's inputs => (the next state, each residual the body's pullback reads)
///   body.pb   : (the gradient of the next state, the residuals) => the tangents
///
/// The forward loop collects what body.fwd returns after the state, one array per residual; the
/// reverse loop runs body.pb once per round, backward, handing it that round's collected values.
/// The residuals are values, not closures, so the tape is ordinary arrays of the residual types.
/// Built when the loop that uses the body is rewritten, because its caller has to know how many
/// slots the tape has and of what types.
struct LoopBodyPlan {
    PullbackPlan graphs; // forward = body.fwd, pullback = body.pb
    /// The name the two graphs were reserved under (`<body>.tape`), so that the graphs of the
    /// reverse pass are named after the same name; a reserved graph is not a graph yet, and its
    /// own name must not be read before the group encodes it.
    std::string name;
    std::vector<Type *> captureTypes;  // the body's capture ports (With), in order
    std::vector<Type *> stateTypes;    // k, the types of the state
    std::vector<Type *> residualTypes; // r, the values the loop collects each round
    std::vector<Type *> portTypes;     // captures, then states: the ports of the call site
    /// The body's result: the state alone for k = 1, the k-tuple otherwise.
    Type *stateValueType = nullptr;
    /// The tangent of stateValueType: the gradient body.pb is seeded with.
    Type *dyType = nullptr;
    /// The tangents of portTypes, skipping the ports without one (the site's pullback result).
    TupleType *tangentsType = nullptr;
    /// One collection slot per residual. A body whose pullback reads no residual still gets one
    /// slot, collecting a constant 1 per round, so the reverse loop knows how many rounds ran.
    std::vector<Type *> collectionTypes;
    std::vector<Type *> collectionElemTypes;
    /// Slots the builder already gave the loop (an empty-array state appended to in place,
    /// Builder::collectLoopAppends): they come before the tape and are not residuals of this
    /// pullback. The tape is what the reverse pass reads.
    std::vector<Type *> existingCollectionTypes;
    std::vector<Type *> existingCollectionElemTypes;
    std::vector<Type *> tapeCollectionTypes;
    /// For each state with no tangent: whether the value the body gives it depends on an
    /// active port. That is a gradient the loop would drop silently (design principles 9.9),
    /// so the loop is refused when it is rewritten.
    std::vector<bool> stateNextTainted;
    bool roundCounter = false;
};

// ---------------------------------------------------------------- forward rewrite

/// Rewrites a graph so that each differentiable call also returns its pullback.
class ForwardRewrite {
  public:
    ForwardRewrite(Engine &engine, GraphDraft &draft, const GraphScope &scope)
        : engine_(engine), draft_(draft), scope_(scope) {}

    void run();

    /// Call sites keyed by the node carrying the call's value.
    const std::unordered_map<gc_node_ref_t, CallSite> &sites() const { return sites_; }

  private:
    /// Brings the functions that function values stand for into this graph, so that the calls
    /// of those values become calls of closures built here (see the definition).
    void followFunctionValues();
    bool carriesFunctionValue(gc_node_ref_t call) const;
    void rewriteBranch(gc_node_ref_t join);
    void rewriteCall(gc_node_ref_t call);
    /// Calls of function values whose callee is evident in the graph: records custom-rule call
    /// sites, turns calls of capture-free static functions into direct calls (returned), and
    /// differentiates calls of closures (a function with captured values) through the
    /// function's graph with the captures as arguments.
    std::optional<gc_node_ref_t> rewriteIndirectCall(gc_node_ref_t call);
    /// A call of the closure `function` with `captures` (caller nodes, in closure order).
    void rewriteClosureCall(
        gc_node_ref_t call, GCGraph *graph, std::vector<gc_node_ref_t> captures,
        gc_node_ref_t calleeValue);
    /// The function `callee` computes when it applies one of autodiff's own macros (grad,
    /// with_grad, vjp) to static functions, else nullptr.
    ::Function *macroCallee(gc_node_ref_t callee);
    /// Makes `node` return `plan`'s (value, pullback) and splits the pair for its users.
    void split(gc_node_ref_t node, const PullbackPlan &plan, std::vector<gc_node_ref_t> inputs);
    /// Gives a LOOP node collection slots for what its body's pullback reads, and records it as a
    /// call site whose pullback is the synthesized reverse loop (design principles 3.10, 9.9).
    void rewriteLoop(gc_node_ref_t loop);
    /// The bookkeeping split needs: the users of `node`'s value read `value` instead, and the
    /// graph's own result is `value` where it was `node`. Shared with rewriteLoop, which makes
    /// the loop's value the state alone (the tapes are not part of what its users see).
    void redirectValue(gc_node_ref_t node, gc_node_ref_t value);
    void retarget(gc_node_ref_t func, const PullbackPlan &plan);

    Engine &engine_;
    GraphDraft &draft_;
    const GraphScope &scope_;
    std::unordered_map<gc_node_ref_t, CallSite> sites_;
    std::deque<mm::RootHandle> roots_;
    std::deque<LoopSite> loopSites_;
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

/// A call of a function value that the gradient has to pass and cannot.
struct Refusal {
    uint64_t origin = 0;
    std::string reason;
};

/// Emits the backward pass of a forward graph into a target graph: the forward graph itself
/// (grad of the top-level function), or a separate pullback graph that captures the primal
/// values it reads.
///
/// In the tape mode the pullback takes the residuals as ordinary parameters instead of capturing
/// them, and hands them back in the order it first read them: that is how a loop's body pullback
/// runs over one round's collected values (design principles 9.9).
class Backward final : public VjpBuilder {
  public:
    Backward(
        GraphDraft &forward, GraphDraft &target,
        const std::unordered_map<gc_node_ref_t, CallSite> &sites, const GraphScope &scope,
        std::vector<Refusal> &refusals, bool residualPorts = false)
        : forward_(forward), target_(target), sites_(sites), inPlace_(&forward == &target),
          scope_(scope), refusals_(refusals), residualPorts_(residualPorts) {}

    /// Target-graph node holding the primal value `primal`.
    gc_node_ref_t ref(gc_node_ref_t primal);

    /// The gradient of the differentiated result. Aggregate results are split element-wise, as
    /// accumulateInto does for every other whole-value gradient — otherwise visitFill, which
    /// distributes elements, would find none and the seed would stop at the result node.
    void seed(gc_node_ref_t primal, gc_node_ref_t gradient) {
        accumulateInto(gradients_[primal], typeOf(forward_, primal), gradient);
    }
    /// Propagates gradients through the forward nodes `order` (inputs before users).
    void run(const std::vector<gc_node_ref_t> &order);
    /// Full gradient of a primal value; zero when none reached it.
    gc_node_ref_t gradient(gc_node_ref_t primal);

    /// Whether `node` is a value that depends on a differentiated parameter but has no
    /// gradient channel of its own (design principles 9.9). Such a value may be carried as an
    /// aggregate element and read later, but using it as data would drop the parameter's
    /// contribution silently, so the backward pass refuses that use (see run).
    bool tainted(gc_node_ref_t node) const { return tainted_.contains(node); }

    /// Primal nodes captured by the target graph, in closure order, after dropping unused ones.
    std::vector<gc_node_ref_t> finishCaptures();
    /// The primal values the target graph reads, in the order its pullback takes them as
    /// parameters. Only in the tape mode (residualPorts).
    const std::vector<gc_node_ref_t> &residuals() const { return captures_; }

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
    vjp_node_t addAccess(vjp_node_t aggregate, size_t index) override {
        return autodiff::addField(target_, typeOf(target_, aggregate), aggregate, index);
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
    void checkVariableReads(const std::vector<gc_node_ref_t> &order);
    /// Refuses `node` when it is a Map or a Set of differentiable values built from an
    /// active value.
    void refuseActiveCollection(gc_node_ref_t node, Type *type);
    /// Refuses `node` when it is an enum built from an active value (see refuseActiveEnum).
    void refuseActiveEnum(gc_node_ref_t node, Type *type);
    /// Refuses `node` when one of its inputs is active: `what` is the place a gradient cannot
    /// pass through, and `remedy` says what to do instead.
    void refuseWhenActive(gc_node_ref_t node, Type *type, const char *what, const char *remedy);
    std::string_view operUri(gc_node_ref_t node) const;
    camel::core::OperId operId(gc_node_ref_t node) const;
    /// Why the gradient cannot pass the call `call` (a CALL, or a FUNC left alone).
    std::string refusalReason(gc_node_ref_t call) const;
    /// Why the function the value `function` is is not known here.
    std::string whyNotEvident(gc_node_ref_t function) const;
    /// An input of `call` (for a closure call, a captured value as well) that is a function
    /// value carrying an active value, or kInvalidNodeRef.
    gc_node_ref_t activeFunctionInput(gc_node_ref_t call) const;
    void reportRefusals(const std::vector<gc_node_ref_t> &order);

    GraphDraft &forward_;
    GraphDraft &target_;
    const std::unordered_map<gc_node_ref_t, CallSite> &sites_;
    const bool inPlace_;
    const GraphScope &scope_;
    std::vector<Refusal> &refusals_;
    std::vector<gc_node_ref_t> refused_;
    std::unordered_set<gc_node_ref_t> primals_;
    std::unordered_set<gc_node_ref_t> active_;
    /// Values that depend on a differentiated parameter but have no tangent of their own: the
    /// contribution of that parameter cannot be carried through them (design principles 9.9).
    std::unordered_set<gc_node_ref_t> tainted_;
    mutable std::unordered_map<gc_node_ref_t, Tangent> gradients_;
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> toTarget_;
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> toPrimal_;
    std::vector<gc_node_ref_t> captures_;
    /// The target graph takes its residuals as parameters instead of capturing them.
    const bool residualPorts_;
};

// ---------------------------------------------------------------- engine

class Engine {
  public:
    Engine(
        const camel::core::context::context_ptr_t &context, const rt::Program *program,
        GCGraph *function)
        : context_(context), group_(context, program), inlining_(context, program, function),
          outerSource_(currentSource) {
        currentSource = context ? context->sourceContext() : nullptr;
    }
    ~Engine() { currentSource = outerSource_; }

    GCGraph *gradient(GCGraph *function, bool withValue);
    const camel::core::context::context_ptr_t &context() const { return context_; }

    /// The session the inlining helpers of the runtime layer work in. Nothing is committed
    /// through it: it only holds the lifted copies of closure graphs they make on the way.
    rt::RuntimeGraphDraftSession &inlining() { return inlining_; }
    /// The graph of a function value, which may have been made in another program (the
    /// gradient a nested grad built): the graphs it calls are read from now on.
    GCGraph *graphOf(const ::Function *function) {
        inlining_.adoptProgram(*function->program());
        return function->graph();
    }
    /// Whether one more call may be brought into a graph to follow a function value. The bound
    /// is on the whole transformation, so following ends for a recursion that passes on
    /// function values without end.
    bool mayFollow() {
        if (followed_ >= kMaxFollowedCalls) {
            return false;
        }
        ++followed_;
        return true;
    }
    bool followingExhausted() const { return followed_ >= kMaxFollowedCalls; }
    static constexpr size_t kMaxFollowedCalls = 256;

    /// The plan differentiating calls of `callee` (memoized; recursion reuses the plan).
    const PullbackPlan &planFor(GCGraph *callee);
    /// A plan for a synthesized graph (a branch arm) with the given types.
    PullbackPlan planForDraft(
        std::unique_ptr<GraphDraft> source, const std::string &name, const PullbackPlan &types,
        GraphScope scope);

    /// The functions whose bodies, brought into a graph, the branch arm `arm` came with.
    const std::unordered_set<GCGraph *> &ownersOf(GCGraph *arm) const {
        static const std::unordered_set<GCGraph *> none;
        auto it = armOwners_.find(arm);
        return it == armOwners_.end() ? none : it->second;
    }
    void noteArmOwner(GCGraph *arm, GCGraph *owner) { armOwners_[arm].insert(owner); }
    /// `function` called itself from one of its own arms while it was being followed.
    void noteRecursion(GCGraph *function) { recursions_.insert(function); }
    /// The plan for calls of a closure over `graph`: the graph with its captured values turned
    /// into trailing norm ports (memoized, so two calls of one lambda share the derivative).
    const PullbackPlan &planForClosure(GCGraph *graph);
    /// The tape plan of a loop `body` (memoized by the body graph). Built at once, not left
    /// pending: the loop's caller needs the shape of the tape to rewrite its node. `stateCount`
    /// and `iterates` are what the LOOP node says the body's parameters are.
    const LoopBodyPlan &loopBodyPlanFor(GCGraph *body, size_t stateCount, bool iterates);
    /// The group of graphs this transformation adds to the program.
    rt::GraphDraftGroup &group() { return group_; }

  private:
    struct Pending {
        PullbackPlan plan;
        std::unique_ptr<GraphDraft> source;
        GraphScope scope;
        GCGraph *graph = nullptr; // the function this is the graph of, for a callee
    };
    [[noreturn]] void refuse();

    PullbackPlan reserve(const std::string &name, const PullbackPlan &types);
    void build(Pending &pending);
    LoopBodyPlan buildLoopBody(GCGraph *body, size_t stateCount, bool iterates);

    camel::core::context::context_ptr_t context_;
    rt::GraphDraftGroup group_;
    std::unordered_map<GCGraph *, PullbackPlan> plans_;
    std::unordered_map<GCGraph *, PullbackPlan> closurePlans_;
    std::unordered_map<GCGraph *, LoopBodyPlan> loopBodyPlans_;
    std::unordered_set<GCGraph *> loopBodiesBeingBuilt_;
    std::deque<Pending> pending_;
    rt::RuntimeGraphDraftSession inlining_;
    size_t followed_ = 0;
    std::unordered_map<GCGraph *, std::unordered_set<GCGraph *>> armOwners_;
    std::unordered_set<GCGraph *> recursions_;
    std::vector<Refusal> refusals_;
    camel::source::source_context_ptr_t outerSource_;
};

// ---------------------------------------------------------------- forward rewrite impl

// A function value is followed to the function it stands for by bringing the code between the
// two into this graph: the body of a function that receives an evident function value (as an
// argument or as a captured value), and the body of a function that returns a function value.
// Afterwards a call of such a value is a call of a closure built in this graph, which
// rewriteIndirectCall differentiates; a value that is still not evident (it is chosen by a
// branch, read from a container, a parameter of the differentiated function) is refused at
// its call as before. Inlining is std::opt::inline's own (camel/passes/opt/inline.h), so the order
// and the effects of the inlined body are kept by the same code as everywhere else.
//
// Bounded: only calls that carry function values are inlined, and a branch arm is never
// inlined: it is a graph of its own, differentiated on its own (rewriteBranch), which knows
// the functions whose bodies it came with (GraphScope::enclosing). A call of one of those from
// inside the arm is the recursion of that function and is left a call, so a recursive function
// is followed into once. Besides, the transformation as a whole follows at most
// Engine::kMaxFollowedCalls calls.
void ForwardRewrite::followFunctionValues() {
    std::unordered_set<gc_node_ref_t> left; // calls that could not be inlined
    bool changed = true;
    while (changed) {
        changed = false;
        std::unordered_set<gc_node_ref_t> arms;
        for (gc_node_ref_t id = 0; id < draft_.nodeSlotCount(); ++id) {
            if (draft_.alive(id) && kindOf(draft_, id) == GCNodeKind::Join) {
                arms.insert(draft_.withInputsOf(id).begin(), draft_.withInputsOf(id).end());
            }
        }
        for (gc_node_ref_t id = 0; id < draft_.nodeSlotCount(); ++id) {
            if (!draft_.alive(id) || left.contains(id) || arms.contains(id)) {
                continue;
            }
            const GCNodeKind kind = kindOf(draft_, id);
            if ((kind != GCNodeKind::Func && kind != GCNodeKind::Call) ||
                !carriesFunctionValue(id) || draft_.isArmCall(id)) {
                continue;
            }
            // The function whose body would come in (for a closure, the function it closes).
            GCGraph *const function =
                kind == GCNodeKind::Func
                    ? calleeOf(draft_, id)
                    : engine_.graphOf(evidentFunction(draft_, draft_.withInputsOf(id).front()));
            if (scope_.enclosing.contains(function)) {
                engine_.noteRecursion(function);
                left.insert(id);
                continue;
            }
            if (!engine_.mayFollow()) {
                return;
            }
            const auto armGraphs = [&] {
                std::unordered_set<GCGraph *> graphs;
                for (gc_node_ref_t join = 0; join < draft_.nodeSlotCount(); ++join) {
                    if (draft_.alive(join) && kindOf(draft_, join) == GCNodeKind::Join) {
                        for (gc_node_ref_t arm : draft_.withInputsOf(join)) {
                            if (kindOf(draft_, arm) == GCNodeKind::Func) {
                                graphs.insert(calleeOf(draft_, arm));
                            }
                        }
                    }
                }
                return graphs;
            };
            const auto armsBefore = armGraphs();
            // A closure call first becomes a direct call of the closure's graph with the
            // captured values as arguments. Should the inlining then fail, that direct call
            // is an ordinary call and is differentiated as one.
            if (kind == GCNodeKind::Call &&
                !rt::devirtualizeStaticCallInDraft(engine_.inlining(), draft_, id, true)) {
                left.insert(id);
                continue;
            }
            if (camel::passes::opt::inlineCallInOrder(engine_.inlining(), draft_, id)) {
                changed = true;
                for (GCGraph *arm : armGraphs()) {
                    if (!armsBefore.contains(arm)) {
                        engine_.noteArmOwner(arm, function);
                    }
                }
            } else {
                left.insert(id);
            }
        }
    }
}

/// Whether a call (FUNC, or CALL of an evident function) passes an evident function value to
/// its callee, as an argument or a captured value, or gets a function value back from it.
bool ForwardRewrite::carriesFunctionValue(gc_node_ref_t call) const {
    const auto evident = [&](gc_node_ref_t node) {
        return evidentFunction(draft_, node) != nullptr;
    };
    // A macro, and a function with a derivative rule of its own, are not differentiated
    // through their bodies.
    const auto opaque = [](const GCGraph *graph) {
        return graph == nullptr || graph->funcType() == nullptr ||
               graph->funcType()->modifiers().macro() || carriesRule(graph);
    };
    GCGraph *graph  = nullptr;
    auto withInputs = draft_.withInputsOf(call);
    if (kindOf(draft_, call) == GCNodeKind::Func) {
        graph = calleeOf(draft_, call);
        if (opaque(graph)) {
            return false;
        }
    } else {
        if (withInputs.empty()) {
            return false;
        }
        const ::Function *function = evidentFunction(draft_, withInputs.front());
        if (function == nullptr || opaque(function->graph())) {
            return false;
        }
        graph = engine_.graphOf(function);
        // Captured values: those the function value holds, and those filled in here.
        if (const auto *held = function->tupleType()) {
            for (size_t i = 0; i < held->size(); ++i) {
                if (isFunctionType(held->typeAt(i))) {
                    return true;
                }
            }
        }
        const gc_node_ref_t closure = ungated(draft_, withInputs.front());
        if (kindOf(draft_, closure) == GCNodeKind::Fill &&
            std::ranges::any_of(draft_.withInputsOf(closure), evident)) {
            return true;
        }
        withInputs = withInputs.subspan(1);
    }
    return isFunctionType(graph->funcType()->exitType()) ||
           std::ranges::any_of(withInputs, evident) ||
           std::ranges::any_of(draft_.normInputsOf(call), evident);
}

void ForwardRewrite::run() {
    followFunctionValues();
    std::vector<gc_node_ref_t> joins;
    std::vector<gc_node_ref_t> calls;
    std::vector<gc_node_ref_t> indirect;
    std::vector<gc_node_ref_t> loops;
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
        case GCNodeKind::Loop:
            loops.push_back(id);
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
    // Loops last: the values they read have then been split already, so the site's inputs are
    // the values the call sites recorded.
    for (gc_node_ref_t loop : loops) {
        rewriteLoop(loop);
    }
}

void ForwardRewrite::retarget(gc_node_ref_t func, const PullbackPlan &plan) {
    draft_.setCalleeGraph(func, plan.forward);
    draft_.setNodeDataType(func, plan.forwardValueType);
    draft_.setNodeDataIndex(func, draft_.allocateRuntimeSlot(plan.forwardValueType));
}

void ForwardRewrite::redirectValue(gc_node_ref_t node, gc_node_ref_t value) {
    draft_.replaceAllValueUses(node, value);
    // Call sites recorded earlier name their inputs by node. A site whose input is this call
    // (a call rewritten before the call that produces its argument: closure calls are, and a
    // direct call may be) must follow the value to the new node, or the gradient it sends
    // back would be accumulated on the (value, pullback) pair and never reach this call.
    for (auto &[site, recorded] : sites_) {
        for (gc_node_ref_t &input : recorded.inputs) {
            if (input == node) {
                input = value;
            }
        }
    }
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
}

void ForwardRewrite::split(
    gc_node_ref_t node, const PullbackPlan &plan, std::vector<gc_node_ref_t> inputs) {
    const gc_node_ref_t value = draft_.addAccsNode(plan.valueType, 0u);
    redirectValue(node, value);
    draft_.setNormInputs(value, std::span<const gc_node_ref_t>(&node, 1));
    const gc_node_ref_t pullback = addAccs(draft_, plan.pullbackType, node, 1);
    sites_.emplace(
        value,
        CallSite{.pullback = pullback, .inputs = std::move(inputs), .plan = plan});
}

// ---------------------------------------------------------------- the tape of a loop

// A differentiated loop keeps its round-wise data in the loop's own collection slots (design
// principles 3.10, 9.9): the forward loop collects, for each residual its body's pullback reads,
// the value that pullback will read again in that round. The tape is therefore the collected
// arrays themselves: the executor appends to them in place, no round copies an array, and
// nothing is kept outside the loop's value. The reverse pass is one loop over those arrays, last
// index first, carrying the gradients of the state and of the captured values; each round runs
// the body's pullback (whose residual ports are that round's collected values) and adds what it
// sends back to the running gradients.
//
// Nothing here writes a second loop driver: the reverse pass is an ordinary LOOP node, so every
// scheduler runs it the way it runs any other loop, and its graphs come from the same builders
// (addPort/addAccs/addOper/addAggregate) as the rest of the pullback.

/// A value without a gradient has no component in the reverse loop's state.
constexpr size_t kNoSlot = static_cast<size_t>(-1);

/// What the three synthesized graphs of a reverse loop need to know.
struct ReverseLoop {
    std::vector<Type *> stateTypes;      // k: the forward loop's state
    std::vector<Type *> captureTypes;    // m: the forward loop's captures
    std::vector<Type *> residualTypes;   // r: what one round collects
    std::vector<Type *> collectionTypes; // Array<residual>, one per collection slot
    Type *stateValueType    = nullptr;   // what the body returns: the state (the k-tuple if k > 1)
    Type *dyType            = nullptr;   // the tangent of stateValueType
    TupleType *tangentsType = nullptr;   // the tangents of the site's ports, in port order
    rt::GraphId bodyPullback =
        rt::kInvalidGraphId; // the forward body's pullback, run once per round
    uint64_t origin         = 0;
    /// The position of a state's / a capture's gradient in the body pullback's result, if it
    /// has one.
    std::vector<std::optional<size_t>> stateTangent;
    std::vector<std::optional<size_t>> captureTangent;
    /// The component of the reverse loop's state a state's / a capture's gradient travels in.
    std::vector<size_t> stateSlot;
    std::vector<size_t> captureSlot;
    /// The component of the state tuple's tangent a state's gradient is.
    std::vector<size_t> statePart;
};

gc_node_ref_t addLoopPort(GraphDraft &draft, Type *type, bool with) {
    const gc_node_ref_t port = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
    if (with) {
        draft.appendWithPort(port);
    } else {
        draft.appendNormPort(port);
    }
    return port;
}

gc_node_ref_t addLoopInt(GraphDraft &draft, int64_t value) {
    return draft.materializeStaticValue(toSlot<int64_t>(value), Type::Int64());
}

/// The signature of one of the reverse loop's graphs; the names are of the compiler's reserved
/// form (`__x__`), so that no name of the program equals one of them.
FunctionType *
loopSignature(const std::vector<Type *> &with, const std::vector<Type *> &norm, Type *exit) {
    FunctionTypeFactory factory;
    for (size_t i = 0; i < with.size(); ++i) {
        factory.addWithArg(std::format("__tape{}__", i), with[i], false);
    }
    static constexpr const char *kNormNames[] = {
        "__round__", "__dstate0__", "__dstate1__", "__dstate2__", "__dstate3__"};
    for (size_t i = 0; i < norm.size(); ++i) {
        factory.addNormArg(
            i < std::size(kNormNames) ? kNormNames[i] : std::format("__dstate{}__", i),
            norm[i],
            false);
    }
    factory.setExitType(exit);
    return factory.build();
}

/// Where the loop is written is stamped on everything the reverse pass runs, so that a diagnostic
/// from inside it (a memory exhaustion, an operator's error) points at the loop.
void stampLoopOrigin(GraphDraft &draft, uint64_t origin) {
    if (origin == 0) {
        return;
    }
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (draft.alive(id)) {
            draft.setNodeOrigin(id, origin);
        }
    }
}

/// The state of a reverse loop: the round index, then the gradients it carries, states first,
/// then captures, as the body pullback's result names them. A value without a tangent has no
/// component: its gradient is never needed.
std::vector<Type *> reverseStateTypes(const ReverseLoop &loop) {
    std::vector<Type *> types{Type::Int64()};
    for (size_t j = 0; j < loop.stateTypes.size(); ++j) {
        if (loop.stateTangent[j]) {
            types.push_back(loop.tangentsType->typeAt(*loop.stateTangent[j]));
        }
    }
    for (size_t c = 0; c < loop.captureTypes.size(); ++c) {
        if (loop.captureTangent[c]) {
            types.push_back(loop.tangentsType->typeAt(*loop.captureTangent[c]));
        }
    }
    return types;
}

/// The tape as the reverse loop's graphs read it: the arrays of the real residuals (the round
/// counter is not a residual, it stays outside the loop).
std::vector<Type *> tapeTypes(const ReverseLoop &loop) {
    return std::vector<Type *>(
        loop.collectionTypes.begin(),
        loop.collectionTypes.begin() + static_cast<ptrdiff_t>(loop.residualTypes.size()));
}

/// A tuple of `values`, or the value itself when there is only one (the loop's value is not
/// wrapped when neither the state nor the collection count wraps it).
gc_node_ref_t packState(
    GraphDraft &draft, const std::vector<Type *> &types, std::span<const gc_node_ref_t> values) {
    ASSERT(types.size() == values.size(), "autodiff: state component count mismatch.");
    if (types.size() == 1) {
        return values.front();
    }
    return addAggregate(draft, TupleType::create(types), values);
}

/// The zero of `primal`'s tangent; `primalValue` is the primal value (a tensor's zero is taken
/// from it).
gc_node_ref_t addZeroTangent(GraphDraft &draft, Type *primal, gc_node_ref_t primalValue) {
    if (isAggregate(primal)) {
        std::vector<gc_node_ref_t> zeros;
        for (size_t i = 0; i < aggregateSize(primal); ++i) {
            if (!tangentElementIndex(primal, i)) {
                continue;
            }
            Type *element = aggregateElement(primal, i);
            zeros.push_back(addZeroTangent(draft, element, addField(draft, primal, primalValue, i)));
        }
        return addAggregate(draft, tangentTypeOf(primal), zeros);
    }
    const TangentSpace *space = DerivativeRegistry::instance().findTangentSpace(primal);
    if (space == nullptr) {
        fail(std::format("no zero gradient for type '{}'", primal->toString()));
    }
    if (space->zero) {
        return draft.materializeStaticValue(*space->zero, tangentTypeOf(primal));
    }
    const std::array<gc_node_ref_t, 1> input{primalValue};
    return addOper(draft, tangentTypeOf(primal), space->zerosLikeUri, input);
}

/// `lhs + rhs` of the tangents of `primal`, element-wise for an aggregate (a state or a captured
/// value may be a tuple, a struct or a model).
gc_node_ref_t addTangentSum(GraphDraft &draft, Type *primal, gc_node_ref_t lhs, gc_node_ref_t rhs) {
    if (isAggregate(primal)) {
        Type *tangent = tangentTypeOf(primal);
        std::vector<gc_node_ref_t> sums;
        for (size_t i = 0; i < aggregateSize(primal); ++i) {
            const auto position = tangentElementIndex(primal, i);
            if (!position) {
                continue;
            }
            Type *element = aggregateElement(primal, i);
            sums.push_back(addTangentSum(
                draft,
                element,
                addField(draft, tangent, lhs, *position),
                addField(draft, tangent, rhs, *position)));
        }
        return addAggregate(draft, tangent, sums);
    }
    const TangentSpace *space = DerivativeRegistry::instance().findTangentSpace(primal);
    if (space == nullptr || space->addUri.empty()) {
        fail(std::format("cannot add gradients of type '{}'", primal->toString()));
    }
    const std::array<gc_node_ref_t, 2> inputs{lhs, rhs};
    return addOper(draft, tangentTypeOf(primal), space->addUri, inputs);
}

/// Whether the reverse loop goes on: one more round while the index is not negative.
std::unique_ptr<GraphDraft> buildReverseCond(const ReverseLoop &loop) {
    const std::vector<Type *> stateTypes = reverseStateTypes(loop);
    const std::vector<Type *> tapes      = tapeTypes(loop);
    auto draft                           = std::make_unique<GraphDraft>();
    draft->setFuncType(loopSignature(tapes, stateTypes, Type::Bool()));
    for (Type *type : tapes) {
        addLoopPort(*draft, type, true);
    }
    std::vector<gc_node_ref_t> ports;
    for (Type *type : stateTypes) {
        ports.push_back(addLoopPort(*draft, type, false));
    }
    const std::array<gc_node_ref_t, 2> compare{ports.front(), addLoopInt(*draft, 0)};
    setResult(*draft, addOper(*draft, Type::Bool(), ":op/ge_l", compare));
    stampLoopOrigin(*draft, loop.origin);
    return draft;
}

/// One round of the reverse pass: read the round's residuals off the tape, run the body's
/// pullback on the gradient of the state this round produced, and add what it sends back to the
/// gradients the loop carries. A state's gradient is what the previous round gets; a capture's
/// gradient accumulates over the rounds.
std::unique_ptr<GraphDraft> buildReverseBody(const ReverseLoop &loop) {
    const std::vector<Type *> stateTypes = reverseStateTypes(loop);
    const std::vector<Type *> tapes      = tapeTypes(loop);
    auto draft                           = std::make_unique<GraphDraft>();
    draft->setFuncType(
        loopSignature(tapes, stateTypes, stateTypes.size() == 1 ? stateTypes.front() : TupleType::create(stateTypes)));
    std::vector<gc_node_ref_t> tapePorts;
    for (Type *type : tapes) {
        tapePorts.push_back(addLoopPort(*draft, type, true));
    }
    std::vector<gc_node_ref_t> ports;
    for (Type *type : stateTypes) {
        ports.push_back(addLoopPort(*draft, type, false));
    }
    // The gradient of the state this round produced, taken out of the components that carry it.
    gc_node_ref_t dy = kInvalidNodeRef;
    if (loop.stateTypes.size() == 1) {
        dy = ports[loop.stateSlot[0]];
    } else {
        std::vector<gc_node_ref_t> components;
        for (size_t j = 0; j < loop.stateTypes.size(); ++j) {
            if (loop.stateSlot[j] != kNoSlot) {
                components.push_back(ports[loop.stateSlot[j]]);
            }
        }
        dy = addAggregate(*draft, tangentTypeOf(loop.stateValueType), components);
    }
    std::vector<gc_node_ref_t> args{dy};
    for (size_t c = 0; c < loop.residualTypes.size(); ++c) {
        const std::array<gc_node_ref_t, 2> at{tapePorts[c], ports.front()};
        args.push_back(addOper(*draft, loop.residualTypes[c], ":op/idx_arr", at));
    }
    const gc_node_ref_t pullback = draft->addFuncNode(loop.bodyPullback, loop.tangentsType);
    draft->setNormInputs(pullback, args);
    draft->addDependency(loop.bodyPullback);
    const std::array<gc_node_ref_t, 2> step{ports.front(), addLoopInt(*draft, 1)};
    std::vector<gc_node_ref_t> next{addOper(*draft, Type::Int64(), ":op/sub_l", step)};
    for (size_t j = 0; j < loop.stateTypes.size(); ++j) {
        if (loop.stateSlot[j] == kNoSlot) {
            continue;
        }
        next.push_back(
            addAccs(*draft, loop.tangentsType->typeAt(*loop.stateTangent[j]), pullback, *loop.stateTangent[j]));
    }
    for (size_t c = 0; c < loop.captureTypes.size(); ++c) {
        if (loop.captureSlot[c] == kNoSlot) {
            continue;
        }
        next.push_back(addTangentSum(
            *draft,
            loop.captureTypes[c],
            ports[loop.captureSlot[c]],
            addAccs(*draft, loop.tangentsType->typeAt(*loop.captureTangent[c]), pullback, *loop.captureTangent[c])));
    }
    setResult(*draft, packState(*draft, stateTypes, next));
    stampLoopOrigin(*draft, loop.origin);
    return draft;
}

/// The reverse pass as one function: given the gradient of the loop's value, start at the last
/// round of the tape with that gradient on the initial state and the zeros of the captures, run
/// the reverse loop, and return the gradients of the site's ports (captures, then states). A loop
/// that ran no round leaves the state's gradient as it came and every capture's zero, which is
/// what a sum over no round contributes.
std::unique_ptr<GraphDraft> buildReverseClosure(
    const ReverseLoop &loop, TupleType *closureType, rt::GraphId cond, rt::GraphId body,
    std::vector<gc_node_ref_t> captures) {
    const std::vector<Type *> stateTypes = reverseStateTypes(loop);
    auto draft                           = std::make_unique<GraphDraft>();
    ASSERT(
        captures.size() == closureType->size(),
        "autodiff: reverse loop capture count mismatch.");
    draft->setFuncType(
        FunctionType::create(param_vec_t{}, param_vec_t{{loop.dyType, false}}, loop.tangentsType));
    draft->setClosureType(closureType);
    const gc_node_ref_t dy = addLoopPort(*draft, loop.dyType, false);
    // What the reverse pass reads from outside: the tape, one array per collection slot, and the
    // captures whose gradient starts at zero (a tensor's zero is taken from the primal).
    std::vector<gc_node_ref_t> tapePorts;
    for (Type *type : loop.collectionTypes) {
        const gc_node_ref_t tape = addPort(*draft, type);
        draft->appendClosureNode(tape);
        tapePorts.push_back(tape);
    }
    std::vector<gc_node_ref_t> capturePorts(loop.captureTypes.size(), kInvalidNodeRef);
    for (size_t c = 0; c < loop.captureTypes.size(); ++c) {
        if (loop.captureSlot[c] == kNoSlot) {
            continue;
        }
        capturePorts[c] = addPort(*draft, loop.captureTypes[c]);
        draft->appendClosureNode(capturePorts[c]);
    }
    // The last round of the tape, and the gradients the first reverse round gets.
    const std::array<gc_node_ref_t, 1> tape{tapePorts.front()};
    const gc_node_ref_t count = addOper(*draft, Type::Int64(), ":struct/len_arr", tape);
    const std::array<gc_node_ref_t, 2> begin{count, addLoopInt(*draft, 1)};
    std::vector<gc_node_ref_t> initial{addOper(*draft, Type::Int64(), ":op/sub_l", begin)};
    for (size_t j = 0; j < loop.stateTypes.size(); ++j) {
        if (loop.stateSlot[j] == kNoSlot) {
            continue;
        }
        initial.push_back(
            loop.stateTypes.size() == 1
                ? dy
                : addAccs(*draft, loop.tangentsType->typeAt(*loop.stateTangent[j]), dy, loop.statePart[j]));
    }
    for (size_t c = 0; c < loop.captureTypes.size(); ++c) {
        if (loop.captureSlot[c] == kNoSlot) {
            continue;
        }
        initial.push_back(addZeroTangent(*draft, loop.captureTypes[c], capturePorts[c]));
    }
    const Type *valueType =
        stateTypes.size() == 1 ? stateTypes.front() : TupleType::create(stateTypes);
    const rt::GCLoopBody payload{
        .condGraph       = cond,
        .bodyGraph       = body,
        .kind            = camel::core::LoopKind::While,
        .collectionCount = 0};
    const gc_node_ref_t loopNode = draft->addLoopNode(payload, const_cast<Type *>(valueType));
    draft->setNormInputs(loopNode, initial);
    draft->setWithInputs(loopNode, std::span<const gc_node_ref_t>(tapePorts.data(), loop.residualTypes.size()));
    draft->addDependency(cond);
    draft->addDependency(body);
    draft->setNodeOrigin(loopNode, loop.origin);
    // The gradients the loop ends with, in the order the site's ports are in.
    std::vector<gc_node_ref_t> stateValues;
    if (stateTypes.size() == 1) {
        stateValues.push_back(loopNode);
    } else {
        for (size_t i = 0; i < stateTypes.size(); ++i) {
            stateValues.push_back(addAccs(*draft, stateTypes[i], loopNode, i));
        }
    }
    std::vector<gc_node_ref_t> tangents;
    for (size_t c = 0; c < loop.captureTypes.size(); ++c) {
        if (loop.captureSlot[c] != kNoSlot) {
            tangents.push_back(stateValues[loop.captureSlot[c]]);
        }
    }
    for (size_t j = 0; j < loop.stateTypes.size(); ++j) {
        if (loop.stateSlot[j] != kNoSlot) {
            tangents.push_back(stateValues[loop.stateSlot[j]]);
        }
    }
    ASSERT(captures.size() == closureType->size(), "autodiff: reverse loop capture count mismatch.");
    setResult(*draft, addAggregate(*draft, loop.tangentsType, tangents));
    stampLoopOrigin(*draft, loop.origin);
    return draft;
}

void ForwardRewrite::rewriteLoop(gc_node_ref_t loop) {
    const LoopDraftView view(*draft_.program(), draft_, loop);
    // The builder may already have given the loop collection slots (an empty-array state whose
    // only use is `append`, Builder::collectLoopAppends). They are part of the loop's value,
    // before the tape, and are preserved here.
    const size_t existingCollections = view.collectionCount();
    const LoopBodyPlan &bodyPlan =
        engine_.loopBodyPlanFor(view.bodyGraph(), view.stateCount(), view.iterable().has_value());
    const size_t k               = bodyPlan.stateTypes.size();
    const size_t m               = bodyPlan.captureTypes.size();
    const size_t r               = bodyPlan.residualTypes.size();
    // A state with no tangent whose next value depends on an active value loses that value's
    // gradient at the round boundary. The body knows which states those are; refuse the loop
    // here, where the source loop is, rather than let the tape skip it (9.9).
    for (size_t j = 0; j < k; ++j) {
        if (!carriesGradient(bodyPlan.stateTypes[j]) && j < bodyPlan.stateNextTainted.size() &&
            bodyPlan.stateNextTainted[j]) {
            failAt(
                draft_.nodeOrigin(loop),
                std::format(
                    "a 'loop' state of type '{}' has no gradient channel, but its next value "
                    "depends on a differentiated parameter{}; the loop would drop that "
                    "parameter's contribution without a diagnostic. Make the state carry a "
                    "gradient (a float, or an aggregate of floats), or stop depending on the "
                    "differentiated value",
                    bodyPlan.stateTypes[j]->toString(),
                    whereIs(draft_.nodeOrigin(loop))));
        }
    }
    // A loop whose value has no tangent cannot carry a gradient, and one none of whose ports has
    // a tangent has nothing to send back: leave it alone (its captures get no gradient, which is
    // right: they cannot influence the value). Such a plan was not given a tape at all, so it
    // has no collection slots; the count is checked once there is a plan to run.
    if (bodyPlan.dyType == nullptr) {
        return;
    }
    bool anyPort = false;
    for (Type *type : bodyPlan.portTypes) {
        anyPort |= carriesGradient(type);
    }
    if (!anyPort) {
        return;
    }
    ASSERT(
        bodyPlan.existingCollectionTypes.size() == existingCollections,
        "autodiff: a loop's existing collection slots do not match its body's.");
    ASSERT(
        bodyPlan.tapeCollectionTypes.size() == (r == 0 ? 1u : r),
        "autodiff: a loop body's tape slots do not match the values its round collects.");
    const PullbackPlan site = makePlanTypes(bodyPlan.portTypes, bodyPlan.stateValueType, carriesGradient);
    // The site's inputs: the captures (the node's With inputs), then the first values of the
    // state (its Norm inputs), the order visitCallSite reads them in.
    std::vector<gc_node_ref_t> inputs(view.captures().begin(), view.captures().end());
    for (size_t j = 0; j < k; ++j) {
        inputs.push_back(view.initialState(j));
    }
    // The loop's value becomes (the states…, one array per collection slot): the tape is part of
    // it, collected in place by the executor.
    std::vector<Type *> valueTypes = bodyPlan.stateTypes;
    valueTypes.insert(valueTypes.end(), bodyPlan.collectionTypes.begin(), bodyPlan.collectionTypes.end());
    Type *const valueType = TupleType::create(valueTypes);
    draft_.setLoopGraphs(loop, draft_.loopGraphsOf(loop).condGraph, bodyPlan.graphs.forward);
    draft_.setLoopCollectionCount(loop, static_cast<uint32_t>(bodyPlan.collectionTypes.size()));
    draft_.setNodeDataType(loop, valueType);
    draft_.setNodeDataIndex(loop, draft_.allocateRuntimeSlot(valueType));
    draft_.addDependency(bodyPlan.graphs.forward);
    // The loop's users see the state alone; the tape is read through the node below. The
    // builder already inserted that projection when it normalised the loop; reuse it rather
    // than redirect it to itself (a cycle), and add one only for a loop that has none.
    gc_node_ref_t state = kInvalidNodeRef;
    for (gc_node_ref_t user : draft_.normUsersOf(loop)) {
        if (k == 1 && kindOf(draft_, user) == GCNodeKind::Accs) {
            state = user;
            break;
        }
        if (k > 1 && kindOf(draft_, user) == GCNodeKind::Fill &&
            typeOf(draft_, user) == bodyPlan.stateValueType) {
            state = user;
            break;
        }
    }
    const uint64_t origin = draft_.nodeOrigin(loop);
    if (state == kInvalidNodeRef) {
        if (k == 1) {
            state = draft_.addAccsNode(bodyPlan.stateTypes.front(), 0u);
        } else {
            std::vector<size_t> slots(k);
            for (size_t j = 0; j < k; ++j) {
                slots[j] = j;
            }
            state = draft_.addFillNode(
                bodyPlan.stateValueType, rt::makeFillPayload(rt::GCFillKind::Tuple, slots));
        }
        draft_.setNodeOrigin(state, origin);
        redirectValue(loop, state);
        if (k == 1) {
            draft_.setNormInputs(state, std::span<const gc_node_ref_t>(&loop, 1));
        } else {
            std::vector<gc_node_ref_t> components;
            for (size_t j = 0; j < k; ++j) {
                const gc_node_ref_t component = addAccs(draft_, bodyPlan.stateTypes[j], loop, j);
                draft_.setNodeOrigin(component, origin);
                components.push_back(component);
            }
            auto *const empty = ::Tuple::create(k, mm::autoSpace());
            const gc_node_ref_t source = draft_.materializeStaticValue(
                toSlot<camel::core::rtdata::Object *>(empty), bodyPlan.stateValueType);
            draft_.setNormInputs(state, std::span<const gc_node_ref_t>(&source, 1));
            draft_.setWithInputs(state, components);
        }
    } else {
        draft_.setNodeOrigin(state, origin);
    }
    std::vector<gc_node_ref_t> tapes;
    for (size_t c = 0; c < bodyPlan.tapeCollectionTypes.size(); ++c) {
        const gc_node_ref_t tape = addAccs(
            draft_, bodyPlan.tapeCollectionTypes[c], loop,
            static_cast<uint32_t>(k + existingCollections + c));
        draft_.setNodeOrigin(tape, origin);
        tapes.push_back(tape);
    }
    // How the reverse pass sees the loop: one slot per gradient it carries, and where in the
    // body pullback's result each comes from.
    ReverseLoop reverse;
    reverse.stateTypes      = bodyPlan.stateTypes;
    reverse.captureTypes    = bodyPlan.captureTypes;
    reverse.residualTypes   = bodyPlan.residualTypes;
    reverse.collectionTypes = bodyPlan.tapeCollectionTypes;
    reverse.stateValueType  = bodyPlan.stateValueType;
    reverse.dyType          = bodyPlan.dyType;
    reverse.tangentsType    = site.tangentsType;
    reverse.bodyPullback    = bodyPlan.graphs.pullback;
    reverse.origin          = origin;
    reverse.stateTangent.resize(k);
    reverse.captureTangent.resize(m);
    reverse.stateSlot.resize(k, kNoSlot);
    reverse.captureSlot.resize(m, kNoSlot);
    reverse.statePart.resize(k, 0);
    size_t slot = 1;
    size_t part = 0;
    for (size_t j = 0; j < k; ++j) {
        if (const auto position = site.tangentIndex(m + j)) {
            reverse.stateTangent[j] = position;
            reverse.stateSlot[j]    = slot++;
            reverse.statePart[j]    = part++;
        }
    }
    for (size_t c = 0; c < m; ++c) {
        if (const auto position = site.tangentIndex(c)) {
            reverse.captureTangent[c] = position;
            reverse.captureSlot[c]    = slot++;
        }
    }
    // The three graphs of the reverse loop, added to the program as the body's tape graphs are.
    // They are reserved (not encoded) now; the group re-points the graph references their payloads
    // hold when it encodes them, as it does for every member the transformation adds.
    const std::string key = bodyPlan.name + ".reverse";
    const rt::GraphId condKey =
        engine_.group().reserve("autodiff.loop/" + key + ".cond", key + ".cond");
    engine_.group().define(condKey, buildReverseCond(reverse));
    const rt::GraphId bodyKey =
        engine_.group().reserve("autodiff.loop/" + key + ".body", key + ".body");
    engine_.group().define(bodyKey, buildReverseBody(reverse));
    const rt::GraphId closureKey = engine_.group().reserve("autodiff.loop/" + key, key);
    // What the reverse pass captures, in order: the tape, then the captures whose gradient it
    // starts from zero.
    std::vector<Type *> closureTypes = bodyPlan.tapeCollectionTypes;
    std::vector<gc_node_ref_t> captures;
    for (gc_node_ref_t tape : tapes) {
        captures.push_back(tape);
    }
    for (size_t c = 0; c < m; ++c) {
        if (reverse.captureSlot[c] != kNoSlot) {
            captures.push_back(view.captures()[c]);
            closureTypes.push_back(bodyPlan.captureTypes[c]);
        }
    }
    TupleType *const closureType = TupleType::create(closureTypes);
    engine_.group().define(
        closureKey,
        buildReverseClosure(reverse, closureType, condKey, bodyKey, captures));
    const gc_node_ref_t pullback =
        addClosure(engine_.group(), draft_, closureKey, site.pullbackType, closureType, captures);
    // The site: the loop's value is `state`; its gradient is the gradient of the loop's value.
    CallSite recorded;
    recorded.pullback = pullback;
    recorded.inputs   = std::move(inputs);
    recorded.plan     = site;
    loopSites_.push_back(LoopSite{.node = loop, .arrays = {}});
    if (const auto iterable = view.iterable()) {
        loopSites_.back().arrays.push_back(LoopArray{.array = *iterable, .iterated = true});
    }
    const auto withInputs = copyOf(draft_.withInputsOf(loop));
    for (gc_node_ref_t captured : withInputs) {
        if (typeOf(draft_, captured)->code() == TypeCode::Array) {
            loopSites_.back().arrays.push_back(LoopArray{.array = captured, .iterated = false});
        }
    }
    recorded.loop = &loopSites_.back();
    sites_.emplace(state, std::move(recorded));
}

std::optional<gc_node_ref_t> ForwardRewrite::rewriteIndirectCall(gc_node_ref_t call) {
    const std::vector<gc_node_ref_t> withInputs = copyOf(draft_.withInputsOf(call));
    if (withInputs.empty()) {
        return std::nullopt;
    }
    // The callee value, behind the gates that order it.
    const gc_node_ref_t fill = ungated(draft_, withInputs.front());
    auto calleeValue         = staticValue(draft_, fill);
    auto *function           = calleeValue ? fromSlot<::Function *>(*calleeValue) : nullptr;
    // A closure built in this graph: FILL(function, captured values...).
    std::vector<gc_node_ref_t> filled; // captured values by closure slot, where filled here
    bool builtHere = false;
    if (function == nullptr && kindOf(draft_, fill) == GCNodeKind::Fill &&
        draft_.normInputsOf(fill).size() == 1) {
        const auto *body = reinterpret_cast<const rt::GCFillBody *>(draft_.payloadOf(fill).data());
        const auto base  = staticValue(draft_, draft_.normInputsOf(fill).front());
        auto *target     = base ? fromSlot<::Function *>(*base) : nullptr;
        if (body->fillKind == rt::GCFillKind::FunctionClosure && target != nullptr &&
            target->graph() != nullptr) {
            engine_.graphOf(target);
            const auto slots  = body->slots();
            const auto values = draft_.withInputsOf(fill);
            const size_t n    = target->graph()->closureNodes().size();
            filled.assign(n, kInvalidNodeRef);
            builtHere = slots.size() == values.size();
            for (size_t k = 0; builtHere && k < slots.size(); ++k) {
                const auto slot = static_cast<size_t>(slots[k]);
                builtHere       = slot < n && filled[slot] == kInvalidNodeRef;
                if (builtHere) {
                    filled[slot] = values[k];
                }
            }
            if (builtHere) {
                function = target;
            }
        }
    }
    if (function == nullptr) {
        // grad(f), with_grad(f) or vjp<rule>(f) of static functions that std::macro has
        // not expanded (autodiff applied inside a function being differentiated).
        function = macroCallee(fill);
    }
    GCGraph *graph = nullptr;
    if (function != nullptr && function->graph() != nullptr) {
        graph = engine_.graphOf(function);
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
        const size_t captureCount = graph->closureNodes().size();
        if (captureCount != 0 || builtHere) {
            // A closure. Its captured values are inputs of the call like any other: those
            // filled here are the nodes that were captured, the rest are the constants the
            // function value already holds.
            std::vector<gc_node_ref_t> captures(captureCount, kInvalidNodeRef);
            const ::Tuple *held = function->tuple();
            for (size_t i = 0; i < captureCount; ++i) {
                if (i < filled.size() && filled[i] != kInvalidNodeRef) {
                    captures[i] = filled[i];
                } else if (held != nullptr && i < held->size()) {
                    captures[i] = draft_.materializeStaticValue(
                        held->get<slot_t>(i),
                        graph->node(graph->closureNodes()[i])->dataType());
                } else {
                    return std::nullopt; // a capture with no value: left to the refusal
                }
            }
            rewriteClosureCall(call, graph, std::move(captures), withInputs.front());
            return std::nullopt;
        }
    } else {
        return std::nullopt; // a function computed at run time
    }
    // A static capture-free function: call its graph directly.
    const gc_node_ref_t direct = draft_.addFuncNode(graph->id(), typeOf(draft_, call));
    draft_.setWithInputs(direct, std::span(withInputs).subspan(1));
    draft_.setNormInputs(direct, copyOf(draft_.normInputsOf(call)));
    // The call ran after its callee value was there; keep what that value waited for.
    std::vector<gc_node_ref_t> ctrl = copyOf(draft_.ctrlInputsOf(call));
    for (gc_node_ref_t before : orderingOf(draft_, withInputs.front())) {
        if (std::find(ctrl.begin(), ctrl.end(), before) == ctrl.end()) {
            ctrl.push_back(before);
        }
    }
    draft_.setCtrlInputs(direct, ctrl);
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

void ForwardRewrite::rewriteClosureCall(
    gc_node_ref_t call, GCGraph *graph, std::vector<gc_node_ref_t> captures,
    gc_node_ref_t calleeValue) {
    FunctionType *type            = graph->funcType();
    std::vector<Type *> portTypes = portTypesOf(graph);
    for (gc_node_ref_t capture : graph->closureNodes()) {
        portTypes.push_back(graph->node(capture)->dataType());
    }
    const bool carriesFlow = tangentTypeOf(type->exitType()) != nullptr && anyTangent(portTypes);
    if (!carriesFlow || type->modifiers().macro()) {
        return; // nothing to differentiate: the call stays, a constant to the reverse pass
    }
    const std::vector<gc_node_ref_t> withInputs = copyOf(draft_.withInputsOf(call));
    std::vector<gc_node_ref_t> norm             = copyOf(draft_.normInputsOf(call));
    norm.insert(norm.end(), captures.begin(), captures.end());
    std::vector<gc_node_ref_t> inputs(withInputs.begin() + 1, withInputs.end());
    inputs.insert(inputs.end(), norm.begin(), norm.end());
    if (inputs.size() != portTypes.size()) {
        fail(
            std::format(
                "call of a closure passes {} values to {} ports and captures{}",
                inputs.size(),
                portTypes.size(),
                whereIs(draft_.nodeOrigin(call))));
    }
    const PullbackPlan &plan = engine_.planForClosure(graph);

    const gc_node_ref_t direct = draft_.addFuncNode(graph->id(), typeOf(draft_, call));
    draft_.setNodeOrigin(direct, draft_.nodeOrigin(call));
    draft_.setWithInputs(direct, std::span(withInputs).subspan(1));
    draft_.setNormInputs(direct, norm);
    // The call ran after the closure was built; keep what the closure waited for.
    std::vector<gc_node_ref_t> ctrl = copyOf(draft_.ctrlInputsOf(call));
    for (gc_node_ref_t before : orderingOf(draft_, calleeValue)) {
        if (std::find(ctrl.begin(), ctrl.end(), before) == ctrl.end()) {
            ctrl.push_back(before);
        }
    }
    draft_.setCtrlInputs(direct, ctrl);
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
    retarget(direct, plan);
    split(direct, plan, std::move(inputs));
}

::Function *ForwardRewrite::macroCallee(gc_node_ref_t callee) {
    if (kindOf(draft_, callee) != GCNodeKind::Oper) {
        return nullptr;
    }
    const auto *body = reinterpret_cast<const rt::GCOperBody *>(draft_.payloadOf(callee).data());
    // The three operators of this module that take a function, recognised by id.
    static const camel::core::OperId kGrad =
        camel::core::OperatorTable::instance().intern("autodiff:grad");
    static const camel::core::OperId kValueAndGrad =
        camel::core::OperatorTable::instance().intern("autodiff:with_grad");
    static const camel::core::OperId kVjp =
        camel::core::OperatorTable::instance().intern("autodiff:vjp");
    const camel::core::OperId oper = body->oper;
    const auto staticFunction      = [&](gc_node_ref_t node) -> ::Function      *{
        const auto value = staticValue(draft_, node);
        auto *function   = value ? fromSlot<::Function *>(*value) : nullptr;
        return function && function->graph() ? function : nullptr;
    };
    const auto norm    = draft_.normInputsOf(callee);
    ::Function *result = nullptr;
    if ((oper == kGrad || oper == kValueAndGrad) && norm.size() == 1) {
        ::Function *function = staticFunction(norm.front());
        if (function == nullptr) {
            return nullptr;
        }
        const GradientGraph gradient = buildGradientGraph(
            engine_.context(),
            function->program(),
            function->graph(),
            oper == kValueAndGrad);
        // The gradient reads the same captured values as f.
        result = ::Function::create(
            gradient.program,
            gradient.graph->id(),
            function->tupleType(),
            mm::autoSpace());
        if (::Tuple *closure = function->tuple()) {
            for (size_t i = 0; i < closure->size(); ++i) {
                result->tuple()->set<slot_t>(i, closure->get<slot_t>(i), function->tupleType());
            }
        }
    } else if (oper == kVjp && norm.size() == 1 && draft_.withInputsOf(callee).size() == 1) {
        ::Function *function = staticFunction(norm.front());
        ::Function *rule     = staticFunction(draft_.withInputsOf(callee).front());
        if (function == nullptr || rule == nullptr) {
            return nullptr;
        }
        result = attachRule(engine_.context(), function, rule);
    } else {
        return nullptr;
    }
    // Kept alive while the transformation runs; it may allocate.
    roots_.emplace_back(mm::autoSpace(), result, result->graph()->funcType(), "autodiff.macro");
    return result;
}

void ForwardRewrite::rewriteCall(gc_node_ref_t call) {
    GCGraph *callee = calleeOf(draft_, call);
    if (callee == nullptr) {
        fail("found a call without a callee graph");
    }
    FunctionType *type     = callee->funcType();
    const auto portTypes   = portTypesOf(callee);
    Type *valueType        = type->exitType();
    const bool carriesFlow = tangentTypeOf(valueType) != nullptr && anyTangent(portTypes);
    if (!carriesFlow || type->modifiers().macro()) {
        return;
    }
    std::vector<gc_node_ref_t> inputs = copyOf(draft_.withInputsOf(call));
    for (gc_node_ref_t input : draft_.normInputsOf(call)) {
        inputs.push_back(input);
    }
    if (inputs.size() != portTypes.size()) {
        fail(
            std::format(
                "call of '{}' passes {} arguments to {} ports",
                callee->name(),
                inputs.size(),
                portTypes.size()));
    }
    const PullbackPlan &plan = engine_.planFor(callee);
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
    // A closure built in this graph that an arm reads is not evident inside the arm, where it
    // is a port. The arm gets the values the closure captured as well and builds the closure
    // again from them, in its own graph: what it then calls is a closure built there, and the
    // gradient reaches the captured values through the arm like through any value it reads.
    std::vector<std::pair<size_t, gc_node_ref_t>> closures; // (position in shared, FILL here)
    for (size_t i = 0; i < shared.size(); ++i) {
        const gc_node_ref_t closure = ungated(draft_, shared[i]);
        if (evidentFunction(draft_, shared[i]) == nullptr ||
            kindOf(draft_, closure) != GCNodeKind::Fill) {
            continue;
        }
        closures.emplace_back(i, closure);
        for (gc_node_ref_t captured : copyOf(draft_.withInputsOf(closure))) {
            if (std::find(shared.begin(), shared.end(), captured) == shared.end()) {
                shared.push_back(captured);
            }
        }
    }
    const auto sharedIndex = [&](gc_node_ref_t node) {
        return static_cast<size_t>(std::find(shared.begin(), shared.end(), node) - shared.begin());
    };
    std::vector<Type *> sharedTypes;
    for (gc_node_ref_t input : shared) {
        sharedTypes.push_back(typeOf(draft_, input));
    }
    const PullbackPlan types = makePlanTypes(sharedTypes, valueType);

    PullbackPlan joined;
    for (gc_node_ref_t arm : arms) {
        GCGraph *armGraph                          = calleeOf(draft_, arm);
        auto variant = GraphDraft::decode(engine_.inlining().work(), armGraph);
        const std::vector<gc_node_ref_t> oldPorts  = copyOf(variant->withPorts());
        const std::vector<gc_node_ref_t> oldInputs = copyOf(draft_.withInputsOf(arm));
        for (gc_node_ref_t port : oldPorts) {
            variant->removeWithPort(port);
        }
        param_vec_t withParams;
        std::vector<gc_node_ref_t> ports;
        for (size_t i = 0; i < shared.size(); ++i) {
            auto it                  = std::find(oldInputs.begin(), oldInputs.end(), shared[i]);
            const gc_node_ref_t port = it != oldInputs.end()
                                           ? oldPorts[static_cast<size_t>(it - oldInputs.begin())]
                                           : addPort(*variant, sharedTypes[i]);
            variant->appendWithPort(port);
            ports.push_back(port);
            withParams.emplace_back(sharedTypes[i], false);
        }
        std::vector<std::pair<gc_node_ref_t, gc_node_ref_t>> rebuilt; // (port, closure)
        for (const auto &[index, closure] : closures) {
            const gc_node_ref_t constant = draft_.normInputsOf(closure).front();
            const gc_node_ref_t base     = variant->materializeStaticValue(
                *staticValue(draft_, constant),
                typeOf(draft_, constant));
            std::vector<gc_node_ref_t> captured;
            for (gc_node_ref_t value : draft_.withInputsOf(closure)) {
                captured.push_back(ports[sharedIndex(value)]);
            }
            const gc_node_ref_t fill =
                variant->addFillNode(typeOf(draft_, closure), draft_.payloadOf(closure));
            variant->setNormInputs(fill, std::span<const gc_node_ref_t>(&base, 1));
            variant->setWithInputs(fill, captured);
            variant->setNodeOrigin(fill, draft_.nodeOrigin(closure));
            rebuilt.emplace_back(ports[index], fill);
        }
        // Afterwards, so that a closure capturing another one captures the one built here.
        for (const auto &[port, fill] : rebuilt) {
            variant->replaceAllValueUses(port, fill);
        }
        FunctionType *armType = armGraph->funcType();
        param_vec_t normParams;
        for (size_t i = 0; i < armType->normTypesCount(); ++i) {
            normParams.emplace_back(armType->normTypeAt(i), false);
        }
        variant->setFuncType(
            FunctionType::create(withParams, normParams, valueType, armType->modifiers()));

        GraphScope scope{
            .role      = GraphRole::BranchArm,
            .enclosing = scope_.enclosing,
            .recursive = scope_.recursive,
        };
        const auto &owners = engine_.ownersOf(armGraph);
        scope.enclosing.insert(owners.begin(), owners.end());
        joined = engine_.planForDraft(std::move(variant), armGraph->name(), types, scope);
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
    } else if (residualPorts_) {
        // The tape mode: the residual is a parameter of the pullback, in the order it is first
        // read, so a loop can hand each round's collected value to the same graph.
        node = addPort(target_, typeOf(forward_, primal));
        target_.appendNormPort(node);
        captures_.push_back(primal);
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
    if (!active_.contains(primal)) {
        return; // a constant, or a value no parameter flows into
    }
    accumulateInto(gradients_[primal], typeOf(forward_, primal), gradient);
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

// A Map or a Set has no tangent, so activity would end at one, and what is read back out of
// it would be taken for a constant: a wrong gradient without a word. A collection that holds
// differentiable values and is built from an active value is refused instead, where it is
// built. One that does not depend on a parameter is a constant like any other.
void Backward::refuseActiveCollection(gc_node_ref_t node, Type *type) {
    const type::CollectionType *coll = type::asCollectionType(type);
    if (coll == nullptr) {
        return;
    }
    const bool differentiable =
        tangentTypeOf(coll->keyType()) != nullptr ||
        (coll->valueType() != nullptr && tangentTypeOf(coll->valueType()) != nullptr);
    if (!differentiable) {
        return;
    }
    refuseWhenActive(node, type, "a Map or a Set", "Keep the differentiated values outside it");
}

// An enum has no tangent either: activity would end at the value built, and the field read out
// of it in a match would be taken for a constant, which is a wrong gradient without a word. An
// enum that is built from an active value is refused, where it is built.
void Backward::refuseActiveEnum(gc_node_ref_t node, Type *type) {
    if (type::asEnumType(type) == nullptr) {
        return;
    }
    refuseWhenActive(node, type, "an enum", "Keep the differentiated values out of it");
}

void Backward::refuseWhenActive(
    gc_node_ref_t node, Type *type, const char *what, const char *remedy) {
    for (auto inputs : {forward_.normInputsOf(node), forward_.withInputsOf(node)}) {
        for (gc_node_ref_t input : inputs) {
            if (active_.contains(input)) {
                failAt(
                    forward_.nodeOrigin(node),
                    "a value that depends on a differentiated parameter is put into " +
                        std::string(what) + " '" + type->toString() +
                        "'. Gradients do not flow through it: there is no derivative rule for "
                        "its operators. " +
                        std::string(remedy));
            }
        }
    }
}

void Backward::run(const std::vector<gc_node_ref_t> &order) {
    primals_.insert(order.begin(), order.end());
    // Activity analysis: a value is active when it has a tangent and depends on a parameter that
    // has one. Gradients only flow between active values, so values derived from constants or
    // integers (e.g. a scale computed from a shape) need no rule and cost nothing.
    std::unordered_set<gc_node_ref_t> ports(
        forward_.withPorts().begin(),
        forward_.withPorts().end());
    ports.insert(forward_.normPorts().begin(), forward_.normPorts().end());
    checkVariableReads(order);
    //
    // A function value has no tangent, but a closure carries the values it captured: one that
    // captured an active value is active too, as a carrier. No gradient is ever accumulated
    // on it; it only makes what is computed from it active, so that a call it is passed to is
    // not mistaken for a constant (see visit, FUNC).
    // An aggregate that has a tangent carries its differentiable elements; a non-differentiable
    // element it was built from (an `int` field of a struct that also has `float` fields) is
    // not a loss as long as nobody reads it as data. Reads of such a field taint the access
    // instead (visitAccs seeds it from its source). A loop with a tangent is the same: its
    // differentiable state can be read, and a read of a state with no channel is the loss.
    const auto structural = [&](gc_node_ref_t node, Type *type) {
        const GCNodeKind kind = kindOf(forward_, node);
        if (kind != GCNodeKind::Fill) {
            return false;
        }
        return (type->code() == TypeCode::Tuple || type->code() == TypeCode::Struct) &&
               tangentTypeOf(type) != nullptr;
    };
    // A branch's condition, its arm selector, and a `where`'s mask are control: the value
    // chooses which data is read, and the reader's gradient flows through the chosen data, not
    // through the choice. Those inputs are exempt from the taint check; reading the condition
    // as data (a cast to float, arithmetic) is not.
    const auto controlInput = [&](gc_node_ref_t node, gc_node_ref_t input) {
        const GCNodeKind kind = kindOf(forward_, node);
        const Type *inputType = typeOf(forward_, input);
        if (kind == GCNodeKind::Join && kindOf(forward_, input) == GCNodeKind::Brch) {
            return true;
        }
        if (kind == GCNodeKind::Brch) {
            return inputType->code() == TypeCode::Bool;
        }
        if (kind == GCNodeKind::Oper) {
            const auto payload = forward_.payloadOf(node);
            if (payload.size() >= sizeof(rt::GCOperBody)) {
                const std::string uri(reinterpret_cast<const rt::GCOperBody *>(payload.data())->uri());
                if (uri == "tensor:where" || uri == ":op/where") {
                    const auto norm = forward_.normInputsOf(node);
                    return !norm.empty() && input == norm.front();
                }
                if (inputType->code() == TypeCode::Bool) {
                    return false;
                }
            }
        }
        return false;
    };
    // A shape or a length is a discrete property of a value, not data whose gradient could
    // flow: using it to reshape or index is control, and differentiating through it is not
    // defined. Such a value is not tainted even when its input is active.
    const auto metadataOper = [&](gc_node_ref_t node) {
        if (kindOf(forward_, node) != GCNodeKind::Oper) {
            return false;
        }
        const auto payload = forward_.payloadOf(node);
        if (payload.size() < sizeof(rt::GCOperBody)) {
            return false;
        }
        const std::string_view uri = reinterpret_cast<const rt::GCOperBody *>(payload.data())->uri();
        return uri == "tensor:shape" || uri == ":op/len_arr" || uri == ":op/len_str" ||
               uri == "tensor:rank" || uri == "tensor:numel";
    };
    for (gc_node_ref_t node : order) {
        Type *const type   = typeOf(forward_, node);
        const bool carrier = holdsFunction(type);
        bool inputActive    = false;
        bool inputTainted   = false;
        for (auto inputs : {forward_.normInputsOf(node), forward_.withInputsOf(node)}) {
            for (gc_node_ref_t input : inputs) {
                inputActive |= active_.contains(input);
                if (!controlInput(node, input)) {
                    inputTainted |= tainted_.contains(input);
                }
            }
        }
        // A variable is state, not a differentiable input: its cell is never active, and a
        // read of it (checked above) is a constant of the call.
        if ((tangentTypeOf(type) == nullptr && !carrier) || isCellType(type)) {
            refuseActiveCollection(node, type);
            refuseActiveEnum(node, type);
            // A loop whose value has no tangent cannot be given a tape: no gradient can flow
            // through the value. It must not be skipped silently when it reads a differentiated
            // value, because that value's contribution would be dropped without a sign of it.
            if (kindOf(forward_, node) == GCNodeKind::Loop) {
                for (auto inputs : {forward_.normInputsOf(node), forward_.withInputsOf(node)}) {
                    for (gc_node_ref_t input : inputs) {
                        if (active_.contains(input) || tainted_.contains(input)) {
                            failAt(
                                forward_.nodeOrigin(node),
                                std::format(
                                    "a 'loop' depends on a differentiated parameter, but its "
                                    "value of type '{}' has no gradient channel. Gradients cannot "
                                    "flow through a loop whose value has no tangent{}; make the "
                                    "loop's value, or the value it reads, something that can carry "
                                    "a gradient",
                                    type->toString(),
                                    whereIs(forward_.nodeOrigin(node))));
                        }
                    }
                }
            }
            // A value that cannot carry a gradient but is computed from one is tainted: it may
            // still be stored, but using it as data is the silent loss design principles 9.9
            // forbids. An aggregate with a tangent is not tainted as a whole; its field read is.
            if (!structural(node, type) && !metadataOper(node) && (inputActive || inputTainted)) {
                tainted_.insert(node);
            }
            continue;
        }
        if (kindOf(forward_, node) == GCNodeKind::Oper &&
            camel::core::oper::isVariableRead(operId(node))) {
            continue;
        }
        // A function passed in from outside is given: it does not depend on a parameter.
        bool active = ports.contains(node) && tangentTypeOf(type) != nullptr;
        active |= inputActive || inputTainted;
        if (!active) {
            continue;
        }
        // The value has a gradient channel, but a data input that has none means the gradient
        // stops here without a diagnostic. Refuse the use rather than return a partial answer.
        if (!structural(node, type) && inputTainted) {
            const gc_node_ref_t source = [&] {
                for (auto inputs : {forward_.normInputsOf(node), forward_.withInputsOf(node)}) {
                    for (gc_node_ref_t input : inputs) {
                        if (tainted_.contains(input)) {
                            return input;
                        }
                    }
                }
                return kInvalidNodeRef;
            }();
            failAt(
                forward_.nodeOrigin(node),
                std::format(
                    "a value of type '{}' depends on a differentiated parameter but has no "
                    "gradient channel, and it is used as data here{}; the parameter's "
                    "contribution would be dropped without a diagnostic. Use a value that can "
                    "carry a gradient (a float, or an aggregate of floats){}",
                    typeOf(forward_, source)->toString(),
                    whereIs(forward_.nodeOrigin(node)),
                    source == kInvalidNodeRef
                        ? std::string()
                        : std::format(
                              "; the value with no channel comes from {}",
                              whereIs(forward_.nodeOrigin(source)))));
        }
        active_.insert(node);
    }
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        auto found = gradients_.find(*it);
        if (found != gradients_.end() && !found->second.empty()) {
            visit(*it);
        }
    }
    reportRefusals(order);
}

// The gradient stops at a call it cannot pass, so the calls before it on the same path are
// never reached, although they are refused for the same reason: they are reported with it, and
// the engine names the first in source order.
void Backward::reportRefusals(const std::vector<gc_node_ref_t> &order) {
    if (refused_.empty()) {
        return;
    }
    std::unordered_set<gc_node_ref_t> before(refused_.begin(), refused_.end());
    std::vector<gc_node_ref_t> work = refused_;
    while (!work.empty()) {
        const gc_node_ref_t node = work.back();
        work.pop_back();
        for (auto inputs : {forward_.normInputsOf(node), forward_.withInputsOf(node)}) {
            for (gc_node_ref_t input : inputs) {
                if (before.insert(input).second) {
                    work.push_back(input);
                }
            }
        }
    }
    const std::unordered_set<gc_node_ref_t> met(refused_.begin(), refused_.end());
    for (gc_node_ref_t node : order) {
        const bool unreached = before.contains(node) && active_.contains(node) &&
                               kindOf(forward_, node) == GCNodeKind::Call &&
                               !sites_.contains(node) &&
                               tangentTypeOf(typeOf(forward_, node)) != nullptr;
        if (met.contains(node) || unreached) {
            refusals_.push_back(
                Refusal{.origin = forward_.nodeOrigin(node), .reason = refusalReason(node)});
        }
    }
}

gc_node_ref_t Backward::activeFunctionInput(gc_node_ref_t call) const {
    const auto carries = [&](gc_node_ref_t node) {
        return holdsFunction(typeOf(forward_, node)) && active_.contains(node);
    };
    auto withInputs = forward_.withInputsOf(call);
    if (kindOf(forward_, call) == GCNodeKind::Call && !withInputs.empty()) {
        const gc_node_ref_t closure = ungated(forward_, withInputs.front());
        if (kindOf(forward_, closure) == GCNodeKind::Fill) {
            for (gc_node_ref_t captured : forward_.withInputsOf(closure)) {
                if (carries(captured)) {
                    return captured;
                }
            }
        }
        withInputs = withInputs.subspan(1);
    }
    for (auto inputs : {withInputs, forward_.normInputsOf(call)}) {
        for (gc_node_ref_t input : inputs) {
            if (carries(input)) {
                return input;
            }
        }
    }
    return kInvalidNodeRef;
}

std::string Backward::refusalReason(gc_node_ref_t call) const {
    static constexpr std::string_view passed =
        "it is passed (as an argument or a captured value) a function value that depends on a "
        "differentiated parameter, and which function that is is not evident where it is "
        "passed: ";
    static constexpr std::string_view head =
        "cannot differentiate through this call of a function value: ";
    if (kindOf(forward_, call) == GCNodeKind::Func) {
        return std::format(
            "cannot differentiate through this call of '{}': {}{}",
            calleeOf(forward_, call)->name(), // left alone, so its callee is a graph
            passed,
            whyNotEvident(activeFunctionInput(call)));
    }
    const auto withInputs = forward_.withInputsOf(call);
    if (withInputs.empty()) {
        return std::string(head) + "the call names no function";
    }
    const gc_node_ref_t callee = ungated(forward_, withInputs.front());
    if (kindOf(forward_, callee) == GCNodeKind::Data ||
        kindOf(forward_, callee) == GCNodeKind::Fill) {
        // The function called is known; the call was left alone all the same.
        if (const gc_node_ref_t input = activeFunctionInput(call); input != kInvalidNodeRef) {
            return std::string(head) + std::string(passed) + whyNotEvident(input);
        }
        return std::string(head) +
               "the function it calls is known, but cannot be differentiated through its body "
               "(a macro, or a function value without a graph)";
    }
    return std::string(head) +
           "which function it calls is not evident where it is called: " + whyNotEvident(callee);
}

std::string Backward::whyNotEvident(gc_node_ref_t function) const {
    function = ungated(forward_, function);
    switch (kindOf(forward_, function)) {
    case GCNodeKind::Port:
        if (!scope_.recursive.empty()) {
            return std::format(
                "the function is a parameter of '{}', a recursive function that passes it on "
                "to itself: a function value is followed into such a function once, not "
                "through its recursive call",
                scope_.recursive);
        }
        switch (scope_.role) {
        case GraphRole::Differentiated:
            return "the function is a parameter, or a captured value, of the function being "
                   "differentiated";
        case GraphRole::BranchArm:
            return "the call is inside a branch arm, and the function comes from outside "
                   "that arm, where it is not a function that is named or built";
        case GraphRole::Callee:
            return "the function is a parameter, or a captured value, of the function making "
                   "the call, and what is passed for it is not a function that is named or "
                   "built where it is passed";
        case GraphRole::LoopBody:
            return "the function is a parameter, or a captured value, of the body of a 'loop' "
                   "the differentiated function runs, and what is passed for it is not a "
                   "function that is named or built where it is passed";
        }
        break;
    case GCNodeKind::Join:
        return "the function is chosen by a branch at run time";
    case GCNodeKind::Accs:
        return "the function is read from a tuple, a struct or an array";
    case GCNodeKind::Oper:
        if (camel::core::oper::isVariableRead(operId(function))) {
            return "the function is read from a variable";
        }
        return std::format("the function is the result of '{}'", operUri(function));
    case GCNodeKind::Func:
        // A call that returns a function value is never retargeted: its callee is a graph.
        return std::format(
            "the function is the result of a call of '{}', and what that returns is not a "
            "function that is named or built in it",
            calleeOf(forward_, function)->name());
    case GCNodeKind::Call:
        return "the function is the result of a call of another function value";
    case GCNodeKind::Data:
    case GCNodeKind::Fill:
        return "the function is known where it is passed, but could not be followed into the "
               "function it is passed to (a macro, a function with a derivative rule of its "
               "own, or a recursion)";
    default:
        break;
    }
    return "the function is computed at run time";
}

std::string_view Backward::operUri(gc_node_ref_t node) const {
    const auto payload = forward_.payloadOf(node);
    return reinterpret_cast<const rt::GCOperBody *>(payload.data())->uri();
}

camel::core::OperId Backward::operId(gc_node_ref_t node) const {
    const auto payload = forward_.payloadOf(node);
    return reinterpret_cast<const rt::GCOperBody *>(payload.data())->oper;
}

// Gradients do not flow through variables. A read of a variable of a differentiable type is
// taken as a constant, which is only right when the content cannot depend on a differentiated
// parameter. That is established here, and only for the differentiated function's own graph,
// where a variable that comes in through a port (a `var` parameter, a captured variable) holds
// plain data of the caller: every operation of this graph that writes the variable a value
// depending on a parameter, or that may write it (a call, a branch arm or a closure that gets
// the variable), must come after the read, i.e. depend on it. Everything else is refused:
// treating such a read as a constant would give a wrong gradient without any sign of it.
void Backward::checkVariableReads(const std::vector<gc_node_ref_t> &order) {
    std::unordered_map<gc_node_ref_t, bool> dependsMemo;
    // Whether a value depends on a parameter that has a tangent (the content of variables
    // aside): the question activity analysis answers for the nodes the result depends on,
    // asked here for a stored value, which the result need not depend on.
    const std::function<bool(gc_node_ref_t)> dependsOnParameter = [&](gc_node_ref_t node) {
        if (auto known = dependsMemo.find(node); known != dependsMemo.end()) {
            return known->second;
        }
        dependsMemo[node] = false; // bounds the recursion; data edges have no cycle
        Type *type        = typeOf(forward_, node);
        bool depends      = false;
        if (!isCellType(type)) {
            if (kindOf(forward_, node) == GCNodeKind::Port) {
                depends = tangentTypeOf(type) != nullptr;
            } else if (!(kindOf(forward_, node) == GCNodeKind::Oper &&
                         camel::core::oper::isVariableRead(operId(node)))) {
                for (auto inputs : {forward_.normInputsOf(node), forward_.withInputsOf(node)}) {
                    for (gc_node_ref_t input : inputs) {
                        depends = depends || dependsOnParameter(input);
                    }
                }
            }
        }
        dependsMemo[node] = depends;
        return depends;
    };
    for (gc_node_ref_t read : order) {
        if (kindOf(forward_, read) != GCNodeKind::Oper ||
            !camel::core::oper::isVariableRead(operId(read)) ||
            tangentTypeOf(typeOf(forward_, read)) == nullptr) {
            continue;
        }
        if (!inPlace_) {
            fail(
                "a variable of a differentiable type is read in a function or branch arm that "
                "the differentiated function calls. Gradients do not flow through variables, "
                "and only a read in the differentiated function itself, before the variable is "
                "written there, can be taken as a constant. Pass the value instead of the "
                "variable");
        }
        const gc_node_ref_t cell = forward_.normInputsOf(read).front();
        if (kindOf(forward_, cell) != GCNodeKind::Port) {
            fail(
                "a variable of a differentiable type that is declared inside the differentiated "
                "function is read there. Gradients do not flow through variables: bind the "
                "value with let instead");
        }
        // Everything that depends on the read (data and control edges) runs after it.
        std::unordered_set<gc_node_ref_t> after{read};
        std::vector<gc_node_ref_t> work{read};
        while (!work.empty()) {
            const gc_node_ref_t node = work.back();
            work.pop_back();
            for (auto users :
                 {forward_.normUsersOf(node),
                  forward_.withUsersOf(node),
                  forward_.ctrlUsersOf(node)}) {
                for (gc_node_ref_t user : users) {
                    if (after.insert(user).second) {
                        work.push_back(user);
                    }
                }
            }
        }
        for (auto users : {forward_.normUsersOf(cell), forward_.withUsersOf(cell)}) {
            for (gc_node_ref_t user : users) {
                if (after.contains(user)) {
                    continue;
                }
                const bool isOper = kindOf(forward_, user) == GCNodeKind::Oper;
                if (isOper && camel::core::oper::isVariableRead(operId(user))) {
                    continue;
                }
                if (isOper && camel::core::OperatorTable::instance().isAssignment(operId(user))) {
                    const auto stored = forward_.normInputsOf(user);
                    if (stored.size() > 1 && !dependsOnParameter(stored[1])) {
                        continue; // plain data: whenever it is stored, the read is a constant
                    }
                    fail(
                        "a variable is read that may hold a value computed from a "
                        "differentiated parameter (the function assigns it one, and the read "
                        "is not ordered before that assignment). Gradients do not flow "
                        "through variables: use the value itself, or read the variable "
                        "before it is assigned");
                }
                fail(
                    "a variable of a differentiable type is read after, or unordered with, a "
                    "call, branch or closure of the differentiated function that receives the "
                    "variable and may write it. Gradients do not flow through variables: read "
                    "it before that point, or pass the value instead of the variable");
            }
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
        // Calls the forward rewrite left alone take no differentiable input: constants. Unless
        // one is passed a function value that carries an active value: its result then depends
        // on the differentiated parameter through a function that could not be followed into
        // the callee. Taking the call for a constant would drop that part of the gradient
        // without any sign of it.
        if (activeFunctionInput(node) != kInvalidNodeRef) {
            refused_.push_back(node);
        }
        return;
    case GCNodeKind::Call:
        // Refused, by Engine::refuse once every graph has been looked at: the first such
        // call in source order is the one reported.
        refused_.push_back(node);
        return;
    case GCNodeKind::Loop:
        // Every loop a gradient reaches was rewritten by ForwardRewrite::rewriteLoop: its value
        // is read through the site it recorded, and the tape it collects carries the backward
        // pass. A loop that was not rewritten has no tangent of its own (nothing can flow through
        // it), so a gradient cannot arrive here. Reaching this is a defect in the rewrite.
        failAt(
            forward_.nodeOrigin(node),
            std::format(
                "cannot differentiate through a 'loop' expression{}: the loop was not given a "
                "tape although a gradient reached it (a defect)",
                whereIs(forward_.nodeOrigin(node))));
    default:
        fail(
            std::format(
                "cannot differentiate through node kind {}",
                static_cast<int>(kindOf(forward_, node))));
    }
}

void Backward::visitCallSite(gc_node_ref_t node, const CallSite &site) {
    if (site.loop != nullptr) {
        // Captured arrays now carry a gradient: an array's tangent is the array of its
        // elements' tangents, so the reverse pass can accumulate into one (design principles
        // 9.9). Iterating an array is still refused: the body receives one element per round,
        // and the reverse pass has no per-round scatter of that element's gradient back into the
        // iterated array. Doing so silently would lose the gradient, so it is located here.
        for (const LoopArray &read : site.loop->arrays) {
            if (!read.iterated || !active_.contains(read.array)) {
                continue;
            }
            const std::string arrayWhere = whereIs(forward_.nodeOrigin(read.array));
            failAt(
                forward_.nodeOrigin(site.loop->node),
                std::format(
                    "a 'loop' iterates over an array that depends on a differentiated "
                    "parameter: {} of type {}{}. The gradient of each round's element would "
                    "have to be scattered back into the array at that round's index, which the "
                    "loop's reverse pass does not do. Capture the array and read its elements "
                    "by index inside the loop instead",
                    arrayWhere.empty() ? "an array" : "the array",
                    typeOf(forward_, read.array)->toString(),
                    arrayWhere));
        }
    }
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
    // Operators without an active input (conversions from integers, arithmetic on shapes) pass
    // no gradient on and need no rule.
    bool active = false;
    for (gc_node_ref_t input : forward_.normInputsOf(node)) {
        active |= active_.contains(input);
    }
    if (!active) {
        return;
    }
    const camel::core::VjpRule rule = DerivativeRegistry::instance().findRule(uri);
    if (rule == nullptr) {
        failAt(
            forward_.nodeOrigin(node),
            std::format(
                "no derivative rule for operator '{}' of type '{}'{}: a differentiated value "
                "passes through it, so its gradient cannot be dropped silently",
                uri,
                typeOf(forward_, node)->toString(),
                whereIs(forward_.nodeOrigin(node))));
    }
    std::vector<gc_node_ref_t> inputs;
    for (gc_node_ref_t input : forward_.normInputsOf(node)) {
        inputs.push_back(ref(input));
    }
    std::vector<gc_node_ref_t> withInputs;
    for (gc_node_ref_t input : forward_.withInputsOf(node)) {
        withInputs.push_back(ref(input));
    }
    rule(
        *this,
        VjpCall{
            .uri        = uri,
            .inputs     = inputs,
            .withInputs = withInputs,
            .output     = ref(node)});
}

void Backward::visitAccs(gc_node_ref_t node) {
    const gc_node_ref_t source = forward_.normInputsOf(node).front();
    Type *sourceType           = typeOf(forward_, source);
    if (!isAggregate(sourceType) || !active_.contains(source)) {
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
    if (body->fillKind == rt::GCFillKind::Array) {
        // An array literal built from active values: the gradient of the array is the array of
        // its elements' gradients, so each element's gradient is read back out at its position
        // (design principles 9.9). An array whose element type has no tangent never has a
        // gradient here.
        Tangent moved = std::move(gradients_[node]);
        if (moved.value == kInvalidNodeRef) {
            return;
        }
        const auto values    = copyOf(forward_.withInputsOf(node));
        const auto slots     = body->slots();
        const auto *array    = tt::as_ptr<type::ArrayType>(type);
        Type *elementTangent = tangentTypeOf(array->elemType());
        if (elementTangent == nullptr) {
            return;
        }
        for (size_t i = 0; i < values.size() && i < slots.size(); ++i) {
            if (!active_.contains(values[i])) {
                continue;
            }
            const gc_node_ref_t index = target_.materializeStaticValue(
                toSlot<int64_t>(static_cast<int64_t>(slots[i])), Type::Int64());
            const std::array<gc_node_ref_t, 2> at{moved.value, index};
            accumulate(
                values[i], autodiff::addOper(target_, elementTangent, ":op/idx_arr", at));
        }
        return;
    }
    if (body->fillKind != rt::GCFillKind::Tuple && body->fillKind != rt::GCFillKind::Struct) {
        fail("cannot differentiate through this kind of literal yet");
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
        if (!active_.contains(values[i])) {
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
        const bool used = !target_.normUsersOf(node).empty() ||
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
    plan.forward      = group_.reserve("autodiff.fwd/" + name + suffix, name + ".fwd");
    plan.pullback     = group_.reserve("autodiff.pb/" + name + suffix, name + ".pb");
    return plan;
}

const PullbackPlan &Engine::planFor(GCGraph *callee) {
    if (auto it = plans_.find(callee); it != plans_.end()) {
        return it->second;
    }
    PullbackPlan plan =
        reserve(callee->name(), makePlanTypes(portTypesOf(callee), callee->funcType()->exitType()));
    pending_.push_back(Pending{
        .plan   = plan,
        .source = GraphDraft::decode(inlining_.work(), callee),
        .scope  = GraphScope{.role = GraphRole::Callee},
        .graph  = callee,
    });
    return plans_.emplace(callee, plan).first->second;
}

const PullbackPlan &Engine::planForClosure(GCGraph *graph) {
    if (auto it = closurePlans_.find(graph); it != closurePlans_.end()) {
        return it->second;
    }
    // The function's graph with its captured values as trailing norm ports: calling it with
    // the captures as extra arguments equals calling the closure.
    auto lifted        = GraphDraft::decode(inlining_.work(), graph);
    FunctionType *type = lifted->funcType();
    param_vec_t withParams, normParams;
    for (size_t i = 0; i < type->withTypesCount(); ++i) {
        withParams.emplace_back(type->withTypeAt(i), type->withIsVarAt(i));
    }
    for (size_t i = 0; i < type->normTypesCount(); ++i) {
        normParams.emplace_back(type->normTypeAt(i), type->normIsVarAt(i));
    }
    std::vector<Type *> portTypes = portTypesOf(graph);
    for (gc_node_ref_t capture : copyOf(lifted->closureNodes())) {
        Type *captured = lifted->header(capture)->dataType;
        lifted->removeClosureNode(capture);
        lifted->appendNormPort(capture);
        normParams.emplace_back(captured, false);
        portTypes.push_back(captured);
    }
    lifted->setFuncType(
        FunctionType::create(withParams, normParams, type->exitType(), type->modifiers()));
    lifted->setClosureType(nullptr);
    PullbackPlan plan =
        reserve(graph->name(), makePlanTypes(std::move(portTypes), type->exitType()));
    pending_.push_back(
        Pending{
            .plan   = plan,
            .source = std::move(lifted),
            .scope  = GraphScope{.role = GraphRole::Callee},
            .graph  = graph,
        });
    return closurePlans_.emplace(graph, plan).first->second;
}

PullbackPlan Engine::planForDraft(
    std::unique_ptr<GraphDraft> source, const std::string &name, const PullbackPlan &types,
    GraphScope scope) {
    PullbackPlan plan = reserve(name, types);
    pending_.push_back(
        Pending{.plan = plan, .source = std::move(source), .scope = std::move(scope)});
    return plan;
}

// Reports the first, in source order, of the calls of function values the gradient has to
// pass and cannot. The diagnostic is located at that call.
void Engine::refuse() {
    struct Place {
        std::string path;
        size_t line   = 0;
        size_t column = 0;
        bool known    = false;
        auto key() const { return std::tie(path, line, column); }
    };
    const auto placeOf = [&](uint64_t origin) {
        Place place;
        if (currentSource && origin != camel::source::kInvalidOriginId) {
            if (const auto *file = currentSource->fileForOrigin(origin)) {
                const auto range = currentSource->resolveOrigin(origin);
                place            = Place{file->path, range.start.line, range.start.character, true};
            }
        }
        return place;
    };
    const Refusal *first = &refusals_.front();
    Place firstPlace     = placeOf(first->origin);
    for (const Refusal &refusal : refusals_) {
        const Place place = placeOf(refusal.origin);
        if (place.known && (!firstPlace.known || place.key() < firstPlace.key())) {
            first      = &refusal;
            firstPlace = place;
        }
    }
    failAt(
        first->origin,
        std::format(
            "{}{}{}. A call of a function value is differentiated when the function is named, "
            "or built by a lambda, in the function being differentiated or in a function it "
            "calls, and gets from there to the call by being bound, captured, passed as an "
            "argument or returned",
            first->reason,
            followingExhausted()
                ? std::format(
                      " (function values were followed through {} calls, the limit, as "
                      "happens when a recursive function passes function values on)",
                      kMaxFollowedCalls)
                : std::string(),
            whereIs(first->origin)));
}

void Engine::build(Pending &pending) {
    const PullbackPlan &plan = pending.plan;
    GraphDraft &forward      = *pending.source;

    if (pending.graph != nullptr && recursions_.contains(pending.graph)) {
        pending.scope.recursive = pending.graph->name();
    }
    ForwardRewrite rewrite(*this, forward, pending.scope);
    rewrite.run();
    const gc_node_ref_t value = valueNode(forward);

    auto pullback = std::make_unique<GraphDraft>();
    pullback->setFuncType(plan.pullbackType);
    const gc_node_ref_t dy = addPort(*pullback, tangentTypeOf(plan.valueType));
    pullback->appendNormPort(dy);

    Backward backward(forward, *pullback, rewrite.sites(), pending.scope, refusals_);
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
        addClosure(group_, forward, plan.pullback, plan.pullbackType, closureType, captures);
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
    forward.setFuncType(
        FunctionType::create(
            withParams,
            normParams,
            plan.forwardValueType,
            sourceType->modifiers()));

    group_.define(plan.forward, std::move(pending.source));
    group_.define(plan.pullback, std::move(pullback));
}

const LoopBodyPlan &Engine::loopBodyPlanFor(GCGraph *body, size_t stateCount, bool iterates) {
    if (auto it = loopBodyPlans_.find(body); it != loopBodyPlans_.end()) {
        ASSERT(
            it->second.stateTypes.size() == stateCount,
            "autodiff: one body graph used by two loops of different shapes.");
        return it->second;
    }
    if (!loopBodiesBeingBuilt_.insert(body).second) {
        fail(
            "a 'loop' whose body needs its own tape (a loop nested in the body of the same loop) "
            "cannot be differentiated");
    }
    LoopBodyPlan plan = buildLoopBody(body, stateCount, iterates);
    loopBodiesBeingBuilt_.erase(body);
    return loopBodyPlans_.emplace(body, std::move(plan)).first->second;
}

// The tape of a loop body: the body as the loop runs it (value => (the next state, the residuals
// its pullback reads)), and the body's pullback with those residuals as parameters. The
// residuals are primal values of the body: the operands the round's rules read again (and, for a
// call in the body, the pullback closure the round's call returned, which is a value like any
// other). They are what the forward loop collects, so the tape is arrays of those types.
LoopBodyPlan Engine::buildLoopBody(GCGraph *body, size_t stateCount, bool iterates) {
    LoopBodyPlan plan;
    if (body == nullptr || body->funcType() == nullptr) {
        fail("a 'loop' whose body has no graph cannot be differentiated");
    }
    const auto withPorts = body->withPorts();
    const auto normPorts = body->normPorts();
    ASSERT(
        normPorts.size() == stateCount + (iterates ? 1u : 0u),
        "autodiff: a 'loop' body does not have the parameters the node says.");
    for (gc_node_ref_t port : withPorts) {
        plan.captureTypes.push_back(body->node(port)->dataType());
    }
    const size_t firstState = iterates ? 1 : 0;
    for (size_t j = 0; j < stateCount; ++j) {
        plan.stateTypes.push_back(body->node(normPorts[firstState + j])->dataType());
    }
    plan.stateValueType = stateCount == 1 ? plan.stateTypes.front() : TupleType::create(plan.stateTypes);
    plan.dyType = carriesGradient(plan.stateValueType) ? tangentTypeOf(plan.stateValueType) : nullptr;
    if (plan.dyType == nullptr) {
        return plan; // nothing can flow through the loop: the caller leaves it alone
    }
    plan.portTypes = plan.captureTypes;
    plan.portTypes.insert(plan.portTypes.end(), plan.stateTypes.begin(), plan.stateTypes.end());
    plan.name   = body->name() + ".tape";
    plan.graphs = reserve(plan.name, makePlanTypes(plan.portTypes, plan.stateValueType, carriesGradient));

    auto source = GraphDraft::decode(inlining_.work(), body);
    // A loop that appends to its array state in place (Builder::markLoopStateInPlace) has one
    // array that every round writes into, so the tape this body is about to collect would hold
    // the same final array in every entry. The loop needs the tape, so its body copies instead:
    // a fallback, not a refusal — the program is legal either way, and the value and the
    // gradients do not change (test/cases/modules/autodiff/loop_grad_in_place.cml).
    rt::fallBackToCopyingAppend(*source);
    const GraphScope scope{.role = GraphRole::LoopBody};
    ForwardRewrite rewrite(*this, *source, scope);
    rewrite.run();
    const gc_node_ref_t value = valueNode(*source);
    // The builder may already have split this body's result into the states and one or more
    // collection slots (Builder::collectLoopAppends). They come first, before the tape; the
    // gradient still flows through the states alone, so the backward pass below is seeded with
    // the state value, not with the whole tuple.
    size_t existingCollections = 0;
    if (kindOf(*source, value) == GCNodeKind::Fill) {
        const auto withIns = source->withInputsOf(value);
        const auto payload = source->payloadOf(value);
        if (payload.size() >= sizeof(rt::GCFillBody)) {
            const auto *fill = reinterpret_cast<const rt::GCFillBody *>(payload.data());
            if (fill->fillKind == rt::GCFillKind::Tuple &&
                fill->slots().size() == withIns.size() && withIns.size() > stateCount) {
                existingCollections = withIns.size() - stateCount;
            }
        }
    }
    std::vector<gc_node_ref_t> stateNext(stateCount, kInvalidNodeRef);
    std::vector<gc_node_ref_t> existingValues;
    for (size_t j = 0; j < stateCount; ++j) {
        stateNext[j] = (stateCount == 1 && existingCollections == 0)
                           ? value
                           : addAccs(*source, plan.stateTypes[j], value, j);
    }
    const auto existingComponents = source->withInputsOf(value);
    for (size_t c = 0; c < existingCollections; ++c) {
        const gc_node_ref_t collection = existingComponents[stateCount + c];
        existingValues.push_back(collection);
        Type *const elementType = typeOf(*source, collection);
        plan.existingCollectionElemTypes.push_back(elementType);
        plan.existingCollectionTypes.push_back(ArrayType::create(elementType));
    }
    const gc_node_ref_t stateValue =
        existingCollections == 0
            ? value
            : (stateCount == 1 ? stateNext.front()
                               : addAggregate(*source, plan.stateValueType, stateNext));

    auto pullback = std::make_unique<GraphDraft>();
    const gc_node_ref_t dy = addPort(*pullback, plan.dyType);
    pullback->appendNormPort(dy);
    Backward backward(*source, *pullback, rewrite.sites(), scope, refusals_, /*residualPorts=*/true);
    backward.seed(stateValue, dy);
    backward.run(valueOrder(*source, stateValue));

    // A state with no tangent that the body computes from an active value would lose that
    // value's gradient at the round boundary. Record it for the caller, which knows the loop
    // and can refuse it with the loop's location.
    plan.stateNextTainted.assign(stateCount, false);
    for (size_t j = 0; j < stateCount; ++j) {
        plan.stateNextTainted[j] = stateNext[j] != kInvalidNodeRef && backward.tainted(stateNext[j]);
    }

    // The tangents of the body's ports, in the order the site's pullback returns them. Asking for
    // them may add residuals (a zero tangent of an aggregate reads the primal), so the residual
    // list is read afterwards: the loop collects what this pullback ended up reading.
    std::vector<gc_node_ref_t> primalPorts(withPorts.begin(), withPorts.end());
    for (size_t j = 0; j < stateCount; ++j) {
        primalPorts.push_back(normPorts[firstState + j]);
    }
    std::vector<gc_node_ref_t> tangents;
    for (size_t i = 0; i < plan.portTypes.size(); ++i) {
        if (plan.graphs.tangentIndex(i)) {
            tangents.push_back(backward.gradient(primalPorts[i]));
        }
    }
    const std::vector<gc_node_ref_t> residuals = backward.residuals();
    for (gc_node_ref_t residual : residuals) {
        plan.residualTypes.push_back(typeOf(*source, residual));
    }
    setResult(*pullback, addAggregate(*pullback, plan.graphs.tangentsType, tangents));
    pruneUnreachable(*pullback);
    pullback->setClosureType(nullptr);
    param_vec_t pullbackParams{{plan.dyType, false}};
    for (Type *residual : plan.residualTypes) {
        pullbackParams.emplace_back(residual, false);
    }
    pullback->setFuncType(
        FunctionType::create(param_vec_t{}, pullbackParams, plan.graphs.tangentsType));

    // The body the loop runs: the next state, then the values one round collects. The builder's
    // collections, if any, stay first; the tape follows.
    std::vector<gc_node_ref_t> components = stateNext;
    components.insert(components.end(), existingValues.begin(), existingValues.end());
    plan.collectionElemTypes = plan.existingCollectionElemTypes;
    for (gc_node_ref_t residual : residuals) {
        components.push_back(residual);
        plan.collectionElemTypes.push_back(typeOf(*source, residual));
    }
    if (residuals.empty()) {
        // Nothing to recollect: one slot counts the rounds, so the reverse pass knows how many
        // there were. It is a value the body produces, not a residual of the pullback.
        plan.roundCounter = true;
        plan.collectionElemTypes.push_back(Type::Int64());
        components.push_back(addLoopInt(*source, 1));
    }
    plan.tapeCollectionTypes.clear();
    for (size_t c = plan.existingCollectionElemTypes.size(); c < plan.collectionElemTypes.size(); ++c) {
        if (plan.collectionElemTypes[c] == nullptr) {
            fail("autodiff: internal error: a loop tape slot has no element type.");
        }
        plan.tapeCollectionTypes.push_back(ArrayType::create(plan.collectionElemTypes[c]));
    }
    for (Type *element : plan.collectionElemTypes) {
        if (element == nullptr) {
            fail("autodiff: internal error: a loop collection has no element type.");
        }
        plan.collectionTypes.push_back(ArrayType::create(element));
    }
    std::vector<Type *> resultTypes = plan.stateTypes;
    resultTypes.insert(resultTypes.end(), plan.collectionElemTypes.begin(), plan.collectionElemTypes.end());
    setResult(*source, addAggregate(*source, TupleType::create(resultTypes), components));
    pruneUnreachable(*source);
    FunctionType *const sourceType = source->funcType();
    param_vec_t withParams, normParams;
    for (size_t i = 0; i < sourceType->withTypesCount(); ++i) {
        withParams.emplace_back(sourceType->withTypeAt(i), sourceType->withIsVarAt(i));
    }
    for (size_t i = 0; i < sourceType->normTypesCount(); ++i) {
        normParams.emplace_back(sourceType->normTypeAt(i), sourceType->normIsVarAt(i));
    }
    source->setFuncType(
        FunctionType::create(withParams, normParams, TupleType::create(resultTypes), sourceType->modifiers()));

    group_.define(plan.graphs.forward, std::move(source));
    group_.define(plan.graphs.pullback, std::move(pullback));
    return plan;
}

GCGraph *Engine::gradient(GCGraph *function, bool withValue) {
    FunctionType *type   = function->funcType();
    const auto signature = gradientSignature(type, withValue);
    // A function the compiler refused (NotDifferentiable) has no signature. Reaching this point
    // with one means the compile-time check was bypassed: an internal defect, not a user error.
    ASSERT(signature.has_value(), "autodiff: a function the compiler accepted has no gradient.");
    if (!signature) {
        fail("autodiff: internal error: a function without a gradient reached the run time.");
    }

    const GraphScope scope{.role = GraphRole::Differentiated};
    auto draft = GraphDraft::decode(inlining_.work(), function);
    ForwardRewrite rewrite(*this, *draft, scope);
    rewrite.run();
    const gc_node_ref_t value              = valueNode(*draft);
    const std::vector<gc_node_ref_t> order = valueOrder(*draft, value);

    Backward backward(*draft, *draft, rewrite.sites(), scope, refusals_);
    const slot_t one = type->exitType()->code() == TypeCode::Float32
                           ? toSlot<camel::core::rtdata::Float32>(1.0f)
                           : toSlot<camel::core::rtdata::Float64>(1.0);
    backward.seed(value, draft->materializeStaticValue(one, type->exitType()));
    backward.run(order);

    // The differential form differentiates the first `with` parameter (9.9); the other `with`
    // parameters are constants and the ordinary parameters are data.
    gc_node_ref_t result = backward.gradient(draft->withPorts().front());
    if (withValue) {
        const std::array<gc_node_ref_t, 2> pair{value, result};
        result = addAggregate(*draft, signature->functionType->exitType(), pair);
    }
    // The gradient function runs the function. Its result is the gradient, which need not
    // depend on everything the function did: what the function's own result waited for (a
    // print after the last value the gradient reads) is not an input of the gradient. The
    // gradient waits for it, so that the function's effects all happen, in their order.
    const gc_node_ref_t ran = draft->exitNode();
    if (ran != kInvalidNodeRef && ran != result && hasOrdering(*draft, ran)) {
        const gc_node_ref_t gate = draft->addGateNode(typeOf(*draft, result));
        draft->setNormInputs(gate, std::span<const gc_node_ref_t>(&result, 1));
        draft->setCtrlInputs(gate, std::span<const gc_node_ref_t>(&ran, 1));
        result = gate;
    }
    setResult(*draft, result);
    pruneUnreachable(*draft);
    draft->setFuncType(signature->functionType);

    const std::string suffix = uniqueSuffix();
    const rt::GraphId key    = group_.reserve(
        "autodiff.grad/" + function->mangledName() + suffix,
        "grad(" + function->name() + ")");
    group_.define(key, std::move(draft));
    while (!pending_.empty()) {
        Pending pending = std::move(pending_.front());
        pending_.pop_front();
        try {
            build(pending);
        } catch (const camel::core::error::RuntimeFault &) {
            if (refusals_.empty()) {
                throw;
            }
            // A graph that cannot be built behind a refused call: the refusal comes first.
            break;
        }
    }
    if (!refusals_.empty()) {
        refuse();
    }
    // The graphs the inlining made on the way are called by members of the group.
    group_.adopt(inlining_.work());
    group_.encode();
    return group_.encoded(key);
}

} // namespace

std::string differentiationRefusal(FunctionType *function) {
    Type *valueType = function->exitType();
    if (valueType == nullptr ||
        (valueType->code() != TypeCode::Float64 && valueType->code() != TypeCode::Float32)) {
        return std::format(
            "it returns {}, not a float", valueType ? valueType->toString() : "nothing");
    }
    // grad differentiates f's first `with` parameter (9.9); the ordinary parameters are data.
    if (function->withTypesCount() == 0) {
        return "it has no `with` parameter, and grad differentiates the first `with` parameter";
    }
    Type *first = function->withTypeAt(0);
    if (first == nullptr) {
        return "its first `with` parameter has no known type";
    }
    if (function->withIsVarAt(0)) {
        return std::format(
            "its first `with` parameter is a `var` of type {}; gradients do not flow through "
            "variables (design principles 3.5)",
            first->toString());
    }
    if (tangentTypeOf(first) == nullptr) {
        return std::format(
            "its first `with` parameter has type {}, which has no gradient (a float, a tensor, "
            "or a struct or tuple of them)",
            first->toString());
    }
    return {};
}

std::optional<GradientSignature> gradientSignature(FunctionType *function, bool withValue) {
    if (!differentiationRefusal(function).empty()) {
        return std::nullopt;
    }
    GradientSignature signature;
    // The first `with` parameter is the differentiated one; every other parameter is passed
    // through so the gradient function runs the function. A `var` parameter stays one, so its
    // caller passes the variable and the function's writes reach it.
    signature.gradientType = tangentTypeOf(function->withTypeAt(0));
    Type *valueType        = function->exitType();
    Type *result           = withValue
                                 ? TupleType::create(std::vector<Type *>{valueType, signature.gradientType})
                                 : signature.gradientType;
    param_vec_t withParams, normParams;
    for (size_t i = 0; i < function->withTypesCount(); ++i) {
        withParams.emplace_back(function->withTypeAt(i), function->withIsVarAt(i));
    }
    for (size_t i = 0; i < function->normTypesCount(); ++i) {
        normParams.emplace_back(function->normTypeAt(i), function->normIsVarAt(i));
    }
    signature.functionType = FunctionType::create(withParams, normParams, result);
    return signature;
}

GradientGraph buildGradientGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    GCGraph *function, bool withValue) {
    // The loops and the arms of the branches are graphs of their own for the differentiation.
    const camel::runtime::Program *outlined =
        camel::runtime::outlineLoopRegions(context, program, function);
    if (outlined != program) {
        function = outlined->graph(function->id());
        program  = outlined;
    }
    const camel::runtime::Program *raised = camel::runtime::raiseBranchArms(context, program, function);
    if (raised != program) {
        function = raised->graph(function->id());
        program  = raised;
    }
    Engine engine(context, program, function);
    GCGraph *graph = engine.gradient(function, withValue);
    // The gradient reads like any program: its arms and its loops are regions.
    GCGraph *root = graph;
    const camel::runtime::Program *lowered =
        camel::passes::opt::lowerBranchArms(context, engine.group().program(), root, true);
    if (lowered != engine.group().program()) {
        graph = lowered->graph(graph->id());
    }
    const camel::runtime::Program *regionized =
        camel::passes::opt::regionizeLoops(context, lowered, graph, true);
    if (regionized != lowered) {
        graph = regionized->graph(graph->id());
    }
    return {regionized, graph};
}

} // namespace camel::autodiff
