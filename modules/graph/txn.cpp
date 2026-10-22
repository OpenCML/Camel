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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Transactions of the graph module. A transaction is a RuntimeGraphDraftSession over a Program:
 * the edits go to drafts of the graphs they name, and `commit` validates the drafts and encodes
 * a new Program that shares the graphs nothing changed. The Camel value is a foreign resource
 * (the session is released by the collector at a safepoint, or at once by commit and abort).
 *
 * The edits normally create no values of the program being processed: replacing a node by
 * another existing node, and erasing one, need none. `add_constant` is the way an edit makes a
 * value (constant folding does): a draft holds the static values of its nodes as raw
 * references, so a value made there must not move or be collected while Camel code runs. A
 * string constant is therefore copied into the permanent space, which is neither collected nor
 * moved (a number is the slot itself); the draft's static slot keeps that reference until
 * `commit` encodes it. Design principles 3.9 and the graph-store note, 4.2.
 */

#include "txn.h"

#include "handle.h"
#include "read.h"

#include "camel/core/error/runtime.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/rtdata/tagged.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/resolver.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/program.h"
#include "src/passes/opt/generic/rewrite.h"

#include <format>

using namespace camel::core::context;
using namespace camel::core::type;
using namespace camel::core::rtdata;
using camel::runtime::GCGraph;
using camel::runtime::GraphCommitConflict;
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
    TxnState(const context_ptr_t &context, const Program *base, GCGraph *root)
        : base(base), session(context, base, root), detached(true), roots(session) {}

    /// The Program the transaction was opened on. The transaction keeps it alive (the foreign
    /// resource traces it) until the transaction is released.
    const Program *base;
    RuntimeGraphDraftSession session;
    bool detached = false;
    bool done     = false;
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
    GraphDraft &draft = state.session.edit(graph);
    if (!draft.alive(node->index())) {
        err::throwRuntimeFault(err::RuntimeDiag::InvalidArgument, site, "the node was removed");
    }
    return {draft, node->index()};
}

/// The Function that is the root of `program`, of the type of `sample`; an error value when the
/// graph has another signature or captures values (a function value made here has no closure).
slot_t functionOf(const Program *program, Type *sample, Context &) {
    static const char *site = "<graph::to_function>";
    const GCGraph *graph = program->root();
    const TupleType *resultType = TupleType::fallible(sample);
    if (graph == nullptr || graph->funcType() == nullptr ||
        !(sample->assignableFrom(graph->funcType()) || graph->funcType()->assignableFrom(sample))) {
        return makeError("SignatureMismatch", "the function has another signature than the sample", site);
    }
    if (!graph->closureNodes().empty()) {
        return makeError("ClosureNotEmpty", "the function captures values", site);
    }
    // The program is held by no one but this call until the function is made.
    mm::NoCollectionScope noCollection;
    static const TupleType *noClosure = TupleType::create();
    auto *function = ::Function::create(program, graph->id(), noClosure, mm::autoSpace());
    return makeOk(toSlot(function), resultType);
}

slot_t openKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    const auto *program = norm.get<const Program *>(0);
    auto *state         = new TxnState(ctx.shared_from_this(), program);
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

/// The new Program, or an error value: `GraphConflict` when a graph the transaction read or
/// wrote has another version in the context's program since, `GraphInvalid` when a graph it
/// edited breaks an invariant (the message names it). The transaction is over either way.
slot_t commitKernel(ArgsView &, ArgsView &norm, Context &) {
    auto *wrapper = norm.get<ForeignResourceObject *>(0);
    if (wrapper->disposed()) {
        return makeError(
            "TxnUsed",
            "the transaction was committed or aborted already",
            "<graph::commit>");
    }
    auto *state = wrapper->resourceAs<TxnState>();
    static const TupleType *resultType = TupleType::fallible(HandleType::get(HandleKind::Program));
    slot_t result;
    try {
        const Program *committed =
            state->detached ? state->session.commit(true) : state->session.commitChecked();
        result = makeOk(toSlot(const_cast<Program *>(committed)), resultType);
    } catch (const GraphCommitConflict &conflict) {
        result = makeError("GraphConflict", conflict.what(), "<graph::commit>");
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &failure) {
        result = makeError("GraphInvalid", failure.what(), "<graph::commit>");
    }
    state->done = true;
    wrapper->dispose();
    return result;
}

/// A transaction over a function value and the graphs it reaches.
slot_t ofKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    auto *function = norm.get<::Function *>(0);
    auto *state =
        new TxnState(ctx.shared_from_this(), function->program(), function->graph());
    return toSlot(ForeignResourceObject::create(mm::autoSpace(), txnDescriptor(), state));
}

/// The function value the transaction of `open_function` makes of its function, of the type of the
/// sample: the commit's errors, and `SignatureMismatch` and `ClosureNotEmpty`.
slot_t toKernel(ArgsView &, ArgsView &norm, Context &ctx) {
    auto *wrapper = norm.get<ForeignResourceObject *>(0);
    if (wrapper->disposed()) {
        return makeError("TxnUsed", "the transaction was committed or aborted already", "<graph::to_function>");
    }
    auto *state = wrapper->resourceAs<TxnState>();
    if (!state->detached) {
        err::throwRuntimeFault(
            err::RuntimeDiag::InvalidArgument,
            "graph.to_function",
            "the transaction is not one of a function; commit it");
    }
    Type *sample = norm.type(1);
    slot_t result;
    try {
        const Program *committed = state->session.commit(true);
        result                   = functionOf(committed, sample, ctx);
    } catch (const mm::MemoryExhausted &) {
        throw;
    } catch (const std::exception &failure) {
        result = makeError("GraphInvalid", failure.what(), "<graph::to_function>");
    }
    state->done = true;
    wrapper->dispose();
    return result;
}

slot_t programOfKernel(ArgsView &, ArgsView &norm, Context &) {
    return toSlot(const_cast<Program *>(norm.get<::Function *>(0)->program()));
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
    GraphDraft &draft                 = state.session.edit(graph);
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
// carries the kernel of each name and nothing else (handover H141).
struct TxnOp {
    const char *name;
    operator_t kernel;
};

/// The type rule of an operator on a function value: `open_function` and the like take one (a function of
/// any type), `to` a transaction and one. The result is as `make` gives it from the type of the
/// last operand.
resolver_ptr_t functionRule(size_t operands, std::string text, Type *(*make)(Type *sample)) {
    return DynamicFuncTypeResolver::create(
        {{0, {}}, {static_cast<int>(operands), std::vector<bool>(operands, false)}},
        std::move(text),
        [operands, make](const type_vec_t &, const type_vec_t &norm, const ModifierSet &)
            -> std::optional<Type *> {
            const bool first = operands == 1 ? norm[0]->code() == TypeCode::Function
                                             : HandleType::is(norm[0], HandleKind::Txn);
            if (!first || norm.back()->code() != TypeCode::Function) {
                return std::nullopt;
            }
            return make(norm.back());
        });
}

const std::vector<TxnOp> &txnOps() {
    static const std::vector<TxnOp> ops = {
        {"open", openKernel},
        {"replace", replaceKernel},
        {"erase", eraseKernel},
        {"commit", commitKernel},
        {"abort", abortKernel},
    };
    return ops;
}

} // namespace

const std::vector<oper_group_ptr_t> &txnGroups() {
    static const std::vector<oper_group_ptr_t> groups = [] {
        std::vector<oper_group_ptr_t> out;
        // The signatures of this file C++ computes (stdlib/graph.cml keeps their reference
        // entries): the result of the first three is the type the function value has.
        out.push_back(OperatorGroup::create(
            "open_function",
            {{"graph:open_function",
              functionRule(1, "(f: F) => Txn", [](Type *) -> Type * {
                  return HandleType::get(HandleKind::Txn);
              })}}));
        out.push_back(OperatorGroup::create(
            "to_function",
            {{"graph:to_function",
              functionRule(2, "(t: Txn, sample: F) => F!", [](Type *sample) -> Type * {
                  return TupleType::fallible(sample);
              })}}));
        out.push_back(OperatorGroup::create(
            "program_of",
            {{"graph:program_of",
              functionRule(1, "(f: F) => Program", [](Type *) -> Type * {
                  return HandleType::get(HandleKind::Program);
              })}}));
        out.push_back(OperatorGroup::create(
            "graph_of",
            {{"graph:graph_of",
              functionRule(1, "(f: F) => Graph", [](Type *) -> Type * {
                  return HandleType::get(HandleKind::Graph);
              })}}));
        return out;
    }();
    return groups;
}

const std::unordered_map<std::string, operator_t> &txnKernels() {
    static const std::unordered_map<std::string, operator_t> kernels = [] {
        std::unordered_map<std::string, operator_t> out;
        for (const TxnOp &op : txnOps()) {
            out.emplace(op.name, op.kernel);
        }
        out.emplace("open_function", ofKernel);
        out.emplace("to_function", toKernel);
        out.emplace("program_of", programOfKernel);
        out.emplace("graph_of", graphOfKernel);
        out.emplace("add_constant", addConstantKernel);
        return out;
    }();
    return kernels;
}

} // namespace camel::graphmod
