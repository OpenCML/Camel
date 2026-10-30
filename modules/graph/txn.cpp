// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Transactions of the graph module. A transaction is a RuntimeGraphDraftSession over a Program:
 * the edits go to drafts of the graphs they name, and `commit` validates the drafts and encodes
 * a new Program that shares the graphs nothing changed. The Camel value is a foreign resource
 * (the session is released by the collector at a safepoint, or at once by commit and abort).
 *
 * The edits normally create no values of the program being processed: replacing a node by
 * another existing node, and erasing one, need none. `add_constant` is the way an edit makes a
 * value the pass already holds, and `eval` makes one the program's own operator computes over
 * constants (a fold does): a draft holds the static values of its nodes as raw references, so a
 * value made there must not move or be collected while Camel code runs. A string constant is
 * therefore copied into the permanent space, which is neither collected nor moved (a number is
 * the slot itself); the draft's static slot keeps that reference until `commit` encodes it.
 * Design principles 3.9 and the graph-store note, 4.2.
 *
 * Design principles 9.3 keeps the semantics on the module's side: a caller names a node and a
 * value, not a place in a list.
 */

#include "txn.h"

#include "handle.h"
#include "read.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm/gen.h"
#include "camel/core/operator.h"
#include "camel/core/operator_table.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/execute/pass/warnings.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/program.h"
#include "src/passes/opt/generic/rewrite.h"

#include <format>

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::runtime::GCGraph;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::GraphDraft;
using camel::runtime::kInvalidNodeRef;
using camel::runtime::Program;
using camel::runtime::RuntimeGraphDraftSession;

namespace camel::graphmod {

namespace {

namespace mm  = camel::core::mm;
namespace err = camel::core::error;

struct TxnState {
    TxnState(const context_ptr_t &context, const Program *base)
        : base(base), session(context, base), roots(session) {}
    /// A transaction over one function and what it reaches, committed to a Program of its own
    /// that is not the context's (the running program is left as it is).
    TxnState(
        const context_ptr_t &context, const Program *base, ::Function *function, Type *functionType)
        : base(base),
          session(context, base, function->graph()),
          detached(true),
          function(function),
          functionType(functionType),
          roots(session) {}

    /// The Program the transaction was opened on. The transaction keeps it alive (the foreign
    /// resource traces it) until the transaction is released.
    const Program *base;
    RuntimeGraphDraftSession session;
    bool detached = false;
    bool done     = false;
    /// The function the transaction was opened on, and its type: the function `commit` makes is
    /// this one over the committed graphs, with the values it captured.
    ::Function *function = nullptr;
    Type *functionType   = nullptr;
    /// The drafts stay open across Camel safepoints (the program can keep using the transaction),
    /// so the session must root them from the first edit to the commit. The descriptor's trace
    /// also calls traceStaticSlots; tracing a draft twice is harmless. TxnState is never moved
    /// (it is reached through the foreign resource), which RootScope requires.
    RuntimeGraphDraftSession::RootScope roots;
};

ForeignResourceDescriptor txnDescriptor() {
    ForeignResourceDescriptor descriptor;
    descriptor.kind    = "graph.txn";
    descriptor.deleter = [](void *resource) { delete static_cast<TxnState *>(resource); };
    descriptor.containsCamelReferences = true;
    descriptor.trace                   = [](void *resource, const Object::RefRelocator &relocate,
                          const Type *type) {
        auto *state = static_cast<TxnState *>(resource);
        if (state->done || !mm::graphSpace().isAllocated(const_cast<Program *>(state->base))) {
            return;
        }
        relocate(
            static_cast<Object *>(const_cast<Program *>(state->base)),
            nullptr,
            RefTraceInfo{
                .owner     = nullptr,
                .ownerType = type,
                .slotType  = nullptr,
                .ownerKind = "Txn",
                .slotName  = "program",
            });
        // The drafts keep the static values of the graphs they edit as raw slots, outside the
        // object graph. A collection at a safepoint must move them with everything else. The
        // session's RootScope (TxnState) registers the same trace for the session's lifetime; a
        // transaction value that is still reachable is traced here as well. Tracing the slots
        // twice is harmless.
        state->session.traceStaticSlots(relocate, type);
        if (state->function != nullptr) {
            state->function = static_cast<::Function *>(relocate(
                state->function,
                state->functionType,
                RefTraceInfo{
                    .owner     = nullptr,
                    .ownerType = type,
                    .slotType  = state->functionType,
                    .ownerKind = "Txn",
                    .slotName  = "function",
                }));
        }
    };
    descriptor.clone   = nullptr;
    descriptor.movable = true;
    return descriptor;
}

/// The state of a transaction that is open; a fault when it was committed or aborted.
TxnState &openState(ArgsView &norm, size_t index, const char *site) {
    auto *wrapper = norm.get<ForeignResourceObject *>(index);
    if (wrapper->disposed()) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the transaction was committed or aborted already");
    }
    return *wrapper->resourceAs<TxnState>();
}

/// The draft of the graph a Node handle names, and the node. The handle must belong to the
/// Program the transaction was opened on.
std::pair<GraphDraft &, camel::runtime::gc_node_ref_t>
draftOf(TxnState &state, const Handle *node, const char *site) {
    if (node->program() != state.base) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the node belongs to another program than the transaction");
    }
    GCGraph *graph = state.base->graph(node->graph());
    if (graph == nullptr || !graph->containsNodeRef(node->index())) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the node is not in its graph");
    }
    auto &draft = state.session.edit(graph).edit();
    if (!draft.alive(node->index())) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the node was removed");
    }
    return {draft, node->index()};
}

/// The function a commit makes: the one the transaction was opened on, over the committed graphs.
/// Its closure is the opened function's, so the values it captured are captured still and a
/// function that captures values commits like one that does not.
slot_t functionOf(TxnState &state, const Program *program, const TupleType *resultType) {
    const GCGraph *graph = program->root();
    if (graph == nullptr || state.function == nullptr) {
        return makeError("GraphInvalid", "the committed program has no root function", "<graph::commit>");
    }
    // The program is held by no one but this call until the function is made.
    mm::NoCollectionScope noCollection;
    auto *function = static_cast<::Function *>(
        state.function->clone(mm::autoSpace(), state.functionType, false));
    function->rebind(program, graph->id());
    return makeOk(toSlot(function), resultType);
}

/// `open(f)`: a transaction over a function and the graphs it reaches. The transaction commits
/// to a program of its own, not the context's: the running program is left as it is.
slot_t openKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    auto *function = norm.get<::Function *>(0);
    auto *state    = new TxnState(ctx.shared_from_this(), function->program(), function, norm.type(0));
    return toSlot(ForeignResourceObject::create(mm::autoSpace(), txnDescriptor(), state));
}

/// Replaces the uses of a node by another node of the same graph and removes the node. What
/// the removed node ordered stays ordered (the same rule the optimizer's rewrites follow).
slot_t replaceKernel(ArgsView &, ArgsView &norm, Context &) {
    TxnState &state = openState(norm, 0, "graph.replace");
    const auto *from = norm.get<Handle *>(1);
    const auto *to   = norm.get<Handle *>(2);
    auto [draft, fromRef] = draftOf(state, from, "graph.replace");
    if (to->program() != state.base || to->graph() != from->graph() ||
        !draft.alive(to->index())) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.replace",
            "the replacement is not a node of the same graph");
    }
    const auto toRef = to->index();
    if (!camel::passes::generic::isReplaceable(draft, fromRef)) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.replace",
            "the node anchors its graph (an exit, an output, a return or the end of a branch arm)");
    }
    if (fromRef == toRef || camel::passes::generic::reaches(draft, fromRef, toRef)) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.replace",
            "the replacement is the node itself or is computed from it");
    }
    camel::passes::generic::replaceNode(draft, fromRef, toRef);
    return NullSlot;
}

/// Removes a node nothing reads the value of.
slot_t eraseKernel(ArgsView &, ArgsView &norm, Context &) {
    TxnState &state = openState(norm, 0, "graph.erase");
    auto [draft, ref] = draftOf(state, norm.get<Handle *>(1), "graph.erase");
    if (!camel::passes::generic::isReplaceable(draft, ref)) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.erase",
            "the node anchors its graph (an exit, an output, a return or the end of a branch arm)");
    }
    if (!draft.normUsersOf(ref).empty() || !draft.withUsersOf(ref).empty()) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.erase",
            "the value of the node is still read");
    }
    camel::passes::generic::replaceNode(draft, ref, kInvalidNodeRef);
    return NullSlot;
}

/// The operands of an operator whose values are static slots of the graph: the argument view a
/// kernel reads, the way the C++ fold builds one (generic.cpp's StaticArgsView).
class StaticArgsView final : public ArgsView {
  public:
    StaticArgsView(std::vector<slot_t> slots, std::vector<Type *> types)
        : slots_(std::move(slots)), types_(std::move(types)) {}
    size_t size() const override { return slots_.size(); }
    slot_t slot(size_t index) const override { return slots_[index]; }
    void setSlot(size_t index, slot_t value) override { slots_[index] = value; }
    TypeCode code(size_t index) const override { return types_[index]->code(); }
    Type *type(size_t index) const override { return types_[index]; }

  private:
    std::vector<slot_t> slots_;
    std::vector<Type *> types_;
};

/// The static value and its type, or nothing: what `eval` reads its operands from and what the
/// C++ fold's `staticValueOf` reads (minus its walk through a GATE, which the module's reads do
/// not make; docs/technical/45_graph_module.md).
std::optional<std::pair<slot_t, Type *>> staticValue(const GraphDraft &draft, gc_node_ref_t ref) {
    const auto *h = draft.header(ref);
    if (h == nullptr || h->kind != GCNodeKind::Data || h->dataIndex >= 0) {
        return std::nullopt;
    }
    const size_t index = static_cast<size_t>(-h->dataIndex);
    if (index >= draft.staticSlots().size()) {
        return std::nullopt;
    }
    Type *type = draft.staticSlotTypes()[index];
    return std::make_pair(draft.staticSlots()[index], type != nullptr ? type : h->dataType);
}

/// Evaluates the pure operator of `n` over its constant operands and gives the node that holds
/// the result, or null when there is no value to compute. This is what a fold written in Camel
/// needs and the transaction did not have: `constant` reads the value of a constant node and
/// `add_constant` mints a value the pass already holds, while neither evaluates an operator.
/// The result is a node of the graph, not a Camel value, because the value is the graph's.
///
/// Null when `n` is no operation (a conversion, a copy, a fill and an access are not evaluated
/// here; the C++ fold has a case for each), when an operand is no constant of the graph, when
/// the operator has no kernel, or when evaluating it faults. A fault is not an error value: a
/// constant expression that can only fault is left as written (design principles 3.9), so this
/// raises the warning the C++ fold raises and answers null, and the program behaves the same
/// with and without the fold.
slot_t evalKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    static const char *site = "<graph::eval>";
    TxnState &state         = openState(norm, 0, site);
    auto [draft, ref]       = draftOf(state, norm.get<Handle *>(1), site);
    const auto *header      = draft.header(ref);
    if (header == nullptr || header->kind != GCNodeKind::Oper ||
        header->dataType == nullptr || !draft.withInputsOf(ref).empty()) {
        return NullSlot;
    }
    const auto *body = reinterpret_cast<const GCOperBody *>(draft.payloadOf(ref).data());
    if (body == nullptr ||
        !camel::core::OperatorTable::instance().isPure(body->oper)) {
        return NullSlot;
    }
    std::vector<slot_t> slots;
    std::vector<Type *> types;
    for (gc_node_ref_t in : draft.normInputsOf(ref)) {
        const auto value = staticValue(draft, in);
        if (!value || value->second == nullptr) {
            return NullSlot;
        }
        slots.push_back(value->first);
        types.push_back(value->second);
    }
    operator_t kernel = body->op;
    if (kernel == nullptr) {
        const auto found = ctx.execMgr().find(std::string(body->uri()));
        if (!found) {
            return NullSlot;
        }
        kernel = *found;
    }
    // The kernel of an operator reads its operands by the operator's declared signature: the two
    // argument views are as the scheduler builds them, with the node's own result type.
    StaticArgsView with({}, {});
    StaticArgsView normal(std::move(slots), std::move(types));
    normal.setResultType(header->dataType);
    slot_t result = NullSlot;
    try {
        result = kernel(with, normal, ctx);
    } catch (const camel::core::error::RuntimeFault &fault) {
        raisePassWarning(
            ctx,
            camel::core::error::RuntimeDiag::ConstantExpressionFaults,
            draft.nodeOrigin(ref),
            fault.what());
        return NullSlot;
    } catch (...) {
        return NullSlot; // the failure is reported when the program runs, as the C++ fold leaves it
    }
    GCGraph *graph = state.base->graph(norm.get<Handle *>(1)->graph());
    if (graph == nullptr) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the graph does not exist");
    }
    const gc_node_ref_t value = draft.materializeStaticValue(result, header->dataType);
    return makeSome(
        toSlot(Handle::create(
            HandleKind::Node,
            state.base,
            graph->id(),
            value,
            nullptr,
            mm::autoSpace())),
        TupleType::nullable(HandleType::get(HandleKind::Node)));
}

/// The changed function (of the type that was opened, `F`), or the error value `GraphInvalid` when a
/// graph it edited breaks an invariant (the message names it). The transaction is over either
/// way. The commit is a value: it never installs the context's program, whoever the transaction
/// was opened on (design principles 9.1: a transform gives a new value and changes nothing
/// another holder sees). Only the pass driver installs a program, the one a pass hands on
/// (src/execute/camel_pass.cpp).
slot_t commitKernel(ArgsView &, ArgsView &norm, Context &) {
    auto *wrapper = norm.get<ForeignResourceObject *>(0);
    if (wrapper->disposed()) {
        return makeError(
            "TxnUsed",
            "the transaction was committed or aborted already",
            "<graph::commit>");
    }
    auto *state = wrapper->resourceAs<TxnState>();
    const TupleType *resultType = asFallible(norm.resultType());
    ASSERT(resultType != nullptr, "commit answers an F!");
    slot_t result;
    try {
        const Program *committed = state->session.commit(true);
        result                   = functionOf(*state, committed, resultType);
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &failure) {
        result = makeError("GraphInvalid", failure.what(), "<graph::commit>");
    }
    state->done = true;
    wrapper->dispose();
    return result;
}

slot_t graphOfKernel(ArgsView &, ArgsView &norm, Context &) {
    auto *function = norm.get<::Function *>(0);
    return toSlot(Handle::create(
        HandleKind::Graph,
        function->program(),
        function->graphId(),
        0,
        nullptr,
        mm::autoSpace()));
}

slot_t abortKernel(ArgsView &, ArgsView &norm, Context &) {
    auto *wrapper = norm.get<ForeignResourceObject *>(0);
    if (!wrapper->disposed()) {
        wrapper->resourceAs<TxnState>()->done = true;
        wrapper->dispose();
    }
    return NullSlot;
}

/// The node a transaction added for `value` in `graph`. A string is copied into the permanent
/// space first: the draft's static slots are raw references outside the object graph, and a
/// transaction value that has become old may not be traced at every safepoint (its wrapper is
/// not moved, and a foreign resource's references are visited when the wrapper is). The
/// permanent space is never collected nor moved, so the new value is safe without that tracing;
/// a number is the slot itself. The transaction's tracer handles the other static values a
/// draft holds (TxnState's descriptor, `traceStaticSlots`).
slot_t addConstantKernel(ArgsView &, ArgsView &norm, Context &) {
    static const char *site = "<graph::add_constant>";
    TxnState &state         = openState(norm, 0, site);
    const auto *handle      = norm.get<Handle *>(1);
    Type *type              = norm.type(2);
    if (handle->kind() != HandleKind::Graph || handle->program() != state.base) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the graph belongs to another program than the transaction");
    }
    GCGraph *graph = state.base->graph(handle->graph());
    if (graph == nullptr) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the graph does not exist");
    }
    slot_t value = norm.slot(2);
    if (type->code() == TypeCode::String) {
        auto *text = fromSlot<::String *>(value);
        value      = toSlot<::String *>(static_cast<::String *>(text->clone(mm::permSpace(), type)));
    }
    auto &draft                 = state.session.edit(graph).edit();
    const camel::runtime::gc_node_ref_t id = draft.materializeStaticValue(value, type);
    return toSlot(Handle::create(
        HandleKind::Node,
        state.base,
        graph->id(),
        id,
        nullptr,
        mm::autoSpace()));
}

// The fixed operators of a transaction: their signature is in stdlib/graph.cml, so the table
// carries the kernel of each name and nothing else.
struct TxnOp {
    const char *name;
    operator_t kernel;
};

const std::vector<TxnOp> &txnOps() {
    static const std::vector<TxnOp> ops = {
        {"open", openKernel},
        {"replace", replaceKernel},
        {"erase", eraseKernel},
        {"commit", commitKernel},
        {"abort", abortKernel},
        {"eval", evalKernel},
    };
    return ops;
}

} // namespace

const std::unordered_map<std::string, operator_t> &txnKernels() {
    static const std::unordered_map<std::string, operator_t> kernels = [] {
        std::unordered_map<std::string, operator_t> out;
        for (const TxnOp &op : txnOps()) {
            out.emplace(op.name, op.kernel);
        }
        out.emplace("graph_of", graphOfKernel);
        out.emplace("add_constant", addConstantKernel);
        return out;
    }();
    return kernels;
}

} // namespace camel::graphmod
