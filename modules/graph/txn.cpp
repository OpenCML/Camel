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
#include "camel/core/rtdata/array.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/execute/executor.h"
#include "camel/execute/pass/warnings.h"
#include "camel/runtime/draft_inline.h"
#include "camel/runtime/draft_outline.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/program.h"
#include "src/passes/opt/generic/rewrite.h"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::runtime::GCGraph;
using camel::runtime::gc_node_ref_t;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
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
    // program-holder-exempt: heap-self-tracking -- the transaction's descriptor trace relocates
    // base as the Txn slot "program", so the collector follows it.
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

/// The transaction that owns the draft of the graph a Node handle names, and the node. The handle
/// must belong to the Program the transaction was opened on. The transaction, not the draft: a
/// caller that hands the draft on to a rewrite loses the only thing that says which graph it edits.
std::pair<camel::runtime::GraphTransaction &, camel::runtime::gc_node_ref_t>
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
    auto &transaction = state.session.edit(graph);
    if (!transaction.edit().alive(node->index())) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the node was removed");
    }
    return {transaction, node->index()};
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
    auto [transaction, fromRef] = draftOf(state, from, "graph.replace");
    auto &draft                = transaction.edit();
    // The replacement names this graph either way it can be named: through the program it came
    // from, or through the transaction that owns the draft a fold wrote it into.
    const bool sameGraph = to->graph() == from->graph();
    const bool knownOwner =
        to->program() == state.base ||
        (to->transaction() != nullptr && to->transaction()->id() == to->graph());
    if (!sameGraph || !knownOwner || !draft.alive(to->index())) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.replace",
            "the replacement is not a node of the same graph");
    }
    const auto toRef = to->index();
    if (!camel::passes::generic::isReplaceable(transaction, fromRef)) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.replace",
            "the node anchors its graph (an exit, an output, a return or the end of a branch arm)");
    }
    if (fromRef == toRef || camel::passes::generic::reaches(transaction, fromRef, toRef)) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.replace",
            "the replacement is the node itself or is computed from it");
    }
    // A named edit of the table (docs/design/graph-editing/README.cn.md 8.1), whose answer to the
    // effect and the order checks is a theorem about `replace` itself: the nodes this touches are
    // therefore not the low-level changes that would make the commit answer for them
    // (`GraphDraft::NamedEdit`).
    const camel::runtime::GraphDraft::NamedEdit named(draft);
    camel::passes::generic::replaceNode(transaction, fromRef, toRef);
    return NullSlot;
}

/// Removes a node nothing reads the value of.
slot_t eraseKernel(ArgsView &, ArgsView &norm, Context &) {
    TxnState &state = openState(norm, 0, "graph.erase");
    auto [transaction, ref] = draftOf(state, norm.get<Handle *>(1), "graph.erase");
    auto &draft             = transaction.edit();
    if (!camel::passes::generic::isReplaceable(transaction, ref)) {
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
    // A named edit: what `erase` re-sequences is the erase's, not a low-level change of the graph
    // (see replaceKernel above).
    const camel::runtime::GraphDraft::NamedEdit named(draft);
    camel::passes::generic::replaceNode(transaction, ref, kInvalidNodeRef);
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
template <class Draft>
std::optional<std::pair<slot_t, Type *>> staticValue(const Draft &draft, gc_node_ref_t ref) {
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
    auto [transaction, ref] = draftOf(state, norm.get<Handle *>(1), site);
    auto &draft             = transaction.edit();
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
    // The node exists in the draft and nowhere else, so the handle that names it names the
    // transaction holding it: a program-backed handle here would claim a published node that
    // does not exist yet.
    return makeSome(
        toSlot(Handle::createInTransaction(transaction, value, mm::autoSpace())),
        TupleType::nullable(HandleType::get(HandleKind::Node)));
}

/// Whether `target` runs before `start`: whether walking up the inputs of `start` -- values, `with`,
/// control, sync and order -- reaches `target`. An input edge `u -> v` says `u` runs before `v`
/// (design principles 3.1), so this is the reading an order edge asks before it is written: an edge
/// that ordered `before` before `after` while the graph already computes `before` from `after` puts
/// the graph on a cycle of runs. The seal check refuses that cycle, and a commit that wrote it would
/// be refused with a message about a graph this operator made; asking here names the operator's own
/// mistake instead.
bool runsBefore(camel::runtime::GraphTransaction &transaction, gc_node_ref_t target,
                gc_node_ref_t start) {
    auto &draft = transaction.edit();
    if (start == target) {
        return true;
    }
    std::vector<gc_node_ref_t> work{start};
    std::vector<bool> seen(draft.nodeSlotCount(), false);
    while (!work.empty()) {
        const gc_node_ref_t node = work.back();
        work.pop_back();
        if (node == target) {
            return true;
        }
        if (node >= seen.size() || seen[node] || draft.header(node) == nullptr) {
            continue;
        }
        seen[node] = true;
        for (auto group :
             {draft.normInputsOf(node),
              draft.withInputsOf(node),
              draft.ctrlInputsOf(node),
              draft.syncInputsOf(node),
              draft.orderInputsOf(node)}) {
            work.insert(work.end(), group.begin(), group.end());
        }
    }
    return false;
}

/// `order(t, before, after)`: adds an order edge, so that when both nodes run `before` runs before
/// `after`. An order edge only orders and never makes a node run (design principles 3.1), which is
/// why the whole promise is the edge and nothing else is written. Refused when the two nodes are not
/// two nodes of one graph, and when the edge would close a cycle; an edge that is there already
/// changes nothing and is not a refusal, because the promise the caller asks for holds.
slot_t orderKernel(ArgsView &, ArgsView &norm, Context &) {
    static const char *site = "graph.order";
    TxnState &state         = openState(norm, 0, site);
    const auto *before      = norm.get<Handle *>(1);
    const auto *after       = norm.get<Handle *>(2);
    auto [transaction, afterRef] = draftOf(state, after, site);
    auto &draft                  = transaction.edit();
    // A node the transaction made (what `eval` and `add_constant` answer) names the transaction and
    // not the program, so "the same graph" is asked of the two handles rather than of the session's
    // program: both must be nodes of that one graph, whichever way they name it.
    if (before->graph() != after->graph() || before->program() != after->program()) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the two nodes are not nodes of the same graph");
    }
    const gc_node_ref_t beforeRef = before->index();
    if (!draft.alive(beforeRef)) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the node was removed");
    }
    if (beforeRef == afterRef) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the two nodes are the same node");
    }
    if (runsBefore(transaction, afterRef, beforeRef)) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the graph computes 'before' from 'after' already: the edge would form a cycle");
    }
    // A named edit: an order edge adds an ordering and never makes a node run, so the table asks
    // this edit no question about the effect or the ordering (`GraphDraft::NamedEdit`).
    {
        const camel::runtime::GraphDraft::NamedEdit named(draft);
        draft.appendOrderDependency(afterRef, beforeRef);
    }
    return NullSlot;
}

/// `inline(t, at, fn)`: the body of the function value `fn` spliced into the region `at` is in,
/// answering the node that holds the body's value. `at` decides the place as well -- the ruling of
/// 2026-11-18 (`e55cd6157`) on docs/handover/questions/2026-11-18-the-at-of-inline-says-a-region-
/// not-a-place.cn.md, and docs/design/graph-editing/README.cn.md 7 and 15.3:
///
/// * `at` is the call `fn` is made at -- the shape `replace(call, inline(call, ...))` expands in
///   place: the body is spliced where the call stood, takes over its place in the block's chain
///   and its order edges, and the call is gone. The call's own arguments are the actuals.
/// * `at` is another statement of its block's chain -- an effect or a node that may fault -- it
///   hands that place over to the code the body becomes and is gone afterwards. `fn` takes no
///   arguments: there is no call site whose arguments could be bound.
/// * `at` stands in no chain -- a pure node -- it only selects the region: the body lands in an
///   Inline region inside `at`'s, and `at` itself and its readers are left as they are. Where the
///   body's writes run among the accesses already there is `order`'s to say, which is the caller's
///   (the operator writes no order edge into that region).
///
/// Only a function value that captures nothing is expanded: the captures of a callee are values of
/// the frame the value was made in, and a splice into another graph cannot name them.
slot_t inlineKernel(ArgsView &, ArgsView &norm, Context &) {
    static const char *site = "graph.inline";
    TxnState &state         = openState(norm, 0, site);
    const auto *at          = norm.get<Handle *>(1);
    auto *function          = norm.get<::Function *>(2);
    GCGraph *calleeGraph    = function != nullptr ? function->graph() : nullptr;
    if (calleeGraph == nullptr) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument, site, "the function value has no graph to expand");
    }
    if (!calleeGraph->closureNodes().empty()) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the function value captures variables: run `std::defunctionalize` on it and expand "
            "the call it leaves");
    }
    // The graph may come from another program than the transaction's (a pass holds the program it
    // was handed and the library it was written in): the draft has to see it to name it.
    if (function->program() != nullptr && function->program() != state.base) {
        state.session.adoptProgram(*function->program());
    }
    auto [transaction, atRef] = draftOf(state, at, site);
    auto &draft               = transaction.edit();
    const camel::runtime::DraftNodeHeader *const atHeader = draft.header(atRef);
    const bool atIsCall = atHeader != nullptr && (atHeader->kind == GCNodeKind::Func ||
                                                  atHeader->kind == GCNodeKind::Call);
    if (atIsCall) {
        if (atHeader->kind == GCNodeKind::Call &&
            !camel::runtime::devirtualizeStaticCallInDraft(state.session, transaction, atRef, true)) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                site,
                "the callee of the call is not known in this graph: run `std::defunctionalize` on "
                "it first");
        }
    }
    // `at` is the call this very function is made at: its arguments are the actuals, and the splice
    // does the whole of the expansion -- the body takes the call's place in the chain and the order
    // edges, and the call is erased (draft_inline.h, inlineCallableInDraft). A call of *another*
    // function is a statement like any other and takes the branch below: what is expanded there is
    // the function value the operator was given.
    if (atIsCall && draft.calleeGraphOf(atRef) == calleeGraph) {
        const camel::runtime::DraftInlineResult inlined =
            camel::runtime::inlineCallableInDraft(state.session, transaction, atRef);
        if (!inlined) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                site,
                "the call cannot be expanded here (a callee that writes a captured variable keeps "
                "the value it hands on, and is left as it is)");
        }
        return toSlot(Handle::createInTransaction(transaction, inlined.valueExit, mm::autoSpace()));
    }
    // Every other `at`: the body is made as a call of its own. `fn` takes no argument, because the
    // operator carries no argument list yet: the declaration would need one.
    if (!calleeGraph->normPorts().empty() || !calleeGraph->withPorts().empty()) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the function value takes arguments: expand it at a call of it, which has them");
    }
    camel::core::type::Type *const valueType = camel::runtime::callValueTypeOf(*calleeGraph, 0);
    if (valueType == nullptr) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument, site, "the function value has no result type");
    }
    const bool takesPlace = camel::runtime::occupiesChainPlace(draft, atRef);
    if (takesPlace && !camel::passes::generic::isReplaceable(transaction, atRef)) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the node anchors its graph (an exit, an output, a return or the end of a branch arm)");
    }
    // The call the body is spliced out of: the splice replaces a call node by the body it names, so
    // the place is written on this call and the splice hands it on to the body's own code. The
    // region is `at`'s either way -- that is the one thing `at` always says.
    const gc_node_ref_t call = draft.addFuncNode(calleeGraph->id(), valueType);
    draft.setNodeRegion(call, draft.nodeRegion(atRef));
    draft.setNodeOrigin(call, draft.nodeOrigin(atRef));
    if (takesPlace) {
        draft.takeStatementPlace(atRef, call);
    }
    const camel::runtime::DraftInlineResult inlined =
        camel::runtime::inlineCallableInDraft(state.session, transaction, call);
    if (!inlined) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            site,
            "the function's body cannot be spliced into this graph");
    }
    if (takesPlace) {
        // The statement is gone and the body's value is what it held: the node `at` named is
        // replaced by the code the body became.
        draft.replaceAllValueUses(atRef, inlined.valueExit);
        draft.replaceAllCtrlUses(atRef, inlined.valueExit);
        draft.eraseNode(atRef);
    }
    return toSlot(Handle::createInTransaction(transaction, inlined.valueExit, mm::autoSpace()));
}

/// `outline(t, ns)`: the group `ns` of one graph becomes a graph of its own and a call of it takes
/// the place of the value the group handed on; that call is what the operator answers. It is
/// `inline`'s reverse (docs/design/graph-editing/README.cn.md 7, 15.3), and the graph it makes is a
/// graph this transaction *made* -- in no program before the edit -- so the commit delivers it
/// through `noteMadeGraph` and not through what the drafts reach (2.5, 8.1).
///
/// What the group's boundary has to be, and how the graph is built from it, is
/// `camel::runtime::outlineDraftNodes` (draft_outline.h): a group of operations whose values come
/// from outside it and go out through one node, no anchor, no access, no ordering or chain place.
/// The boundary is read before anything is made, so a group that is not that is refused with the rule
/// it broke and the draft is as the operator found it.
slot_t outlineKernel(ArgsView &, ArgsView &norm, Context &) {
    static const char *site = "graph.outline";
    TxnState &state         = openState(norm, 0, site);
    auto *array             = norm.get<Array *>(1);
    const size_t count      = array->size();
    if (count == 0) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the group is empty");
    }
    std::vector<gc_node_ref_t> group;
    group.reserve(count);
    camel::runtime::GraphId graphId = camel::runtime::GraphId::Invalid;
    for (size_t i = 0; i < count; ++i) {
        auto *handle = array->get<Handle *>(i);
        if (handle == nullptr || handle->kind() != HandleKind::Node) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument, site, "the group holds a node that is no node");
        }
        // A node read from a draft names the transaction and no program; one read from a program
        // must be this transaction's program.
        if (handle->program() != nullptr && handle->program() != state.base) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                site,
                "a node of the group belongs to another program than the transaction");
        }
        if (graphId == camel::runtime::GraphId::Invalid) {
            graphId = handle->graph();
        } else if (graphId != handle->graph()) {
            err::throwRuntimeFault(
                err::RuntimeDiag::InvalidArgument,
                site,
                "the group names nodes of two graphs: a group is outlined out of one graph");
        }
        group.push_back(handle->index());
    }
    GCGraph *graph = state.base->graph(graphId);
    if (graph == nullptr) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the graph does not exist");
    }
    auto &transaction = state.session.edit(graph);
    for (const gc_node_ref_t ref : group) {
        if (!transaction.edit().alive(ref)) {
            err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "a node was removed");
        }
    }
    const std::string name = std::format(
        "{}$outline{}", graph->name(), static_cast<unsigned>(std::ranges::min(group)));
    try {
        const camel::runtime::DraftOutlineResult outlined =
            camel::runtime::outlineDraftNodes(state.session, transaction, group, name);
        // A node the graph did not have: the call the group's value is now read from, whose callee
        // is the graph this edit made (read back with `callee`).
        return toSlot(Handle::createInTransaction(transaction, outlined.call, mm::autoSpace()));
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &failure) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, failure.what());
    }
}

/// The changed function (of the type that was opened, `F`), or the error value `GraphInvalid` when a
/// graph it edited breaks an invariant (the message names it). The transaction is over either
/// way. The commit is a value: it never installs the context's program, whoever the transaction
/// was opened on (design principles 9.1: a transform gives a new value and changes nothing
/// another holder sees). Only the pass driver installs a program, the one a pass hands on
/// (src/execute/camel_pass.cpp).
slot_t commitKernel(ArgsView &, ArgsView &norm, Context &ctx) {
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
    } catch (const camel::runtime::GraphCommitRefused &refused) {
        // The two commit checks of analysis 4.1 refused the graphs this commit changed: one line,
        // the same one the reading made, recorded where a diagnostic is read and handed back as the
        // error value this `commit` answers, because a commit that cannot keep its promises is a
        // refused commit and not a broken invariant of the graph's edges.
        camel::runtime::reportCommitViolation(
            ctx.shared_from_this(), "graph::commit", refused.violation());
        result = makeError("GraphInvalid", refused.what(), "<graph::commit>");
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
    auto &transaction = state.session.edit(graph);
    auto &draft       = transaction.edit();
    const camel::runtime::gc_node_ref_t id = draft.materializeStaticValue(value, type);
    // The node was just made in the draft, so the handle names the transaction: naming the
    // program would claim a published node that is not there.
    return toSlot(Handle::createInTransaction(transaction, id, mm::autoSpace()));
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
        {"order", orderKernel},
        {"inline", inlineKernel},
        {"outline", outlineKernel},
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
