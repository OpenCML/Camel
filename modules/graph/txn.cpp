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
 * The edits create no values of the program being processed: a draft holds the static values
 * of its nodes as raw references, and a collection that runs while Camel code is running could
 * move one. Replacing a node by another existing node, and erasing one, need none.
 */

#include "txn.h"

#include "handle.h"

#include "camel/core/error/runtime.h"
#include "camel/core/rtdata/foreign.h"
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
        : base(base), session(context, base) {}

    /// The Program the transaction was opened on. The transaction keeps it alive (the foreign
    /// resource traces it) until the transaction is released.
    const Program *base;
    RuntimeGraphDraftSession session;
    bool done = false;
};

ForeignResourceDescriptor txnDescriptor() {
    ForeignResourceDescriptor descriptor;
    descriptor.kind    = "graph.txn";
    descriptor.deleter = [](void *resource) { delete static_cast<TxnState *>(resource); };
    descriptor.containsCamelReferences = true;
    descriptor.trace                   = [](void *resource, const Object::RefRelocator &relocate,
                          const Type *type) {
        const auto *state = static_cast<TxnState *>(resource);
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
        const Program *committed = state->session.commitChecked();
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

slot_t abortKernel(ArgsView &, ArgsView &norm, Context &) {
    auto *wrapper = norm.get<ForeignResourceObject *>(0);
    if (!wrapper->disposed()) {
        wrapper->resourceAs<TxnState>()->done = true;
        wrapper->dispose();
    }
    return NullSlot;
}

struct TxnOp {
    const char *name;
    operator_t kernel;
    std::vector<Type *> params;
    Type *result;
};

const std::vector<TxnOp> &txnOps() {
    static const std::vector<TxnOp> ops = [] {
        Type *program = HandleType::get(HandleKind::Program);
        Type *txn     = HandleType::get(HandleKind::Txn);
        Type *node    = HandleType::get(HandleKind::Node);
        return std::vector<TxnOp>{
            {"open", openKernel, {program}, txn},
            {"replace", replaceKernel, {txn, node, node}, Type::Void()},
            {"erase", eraseKernel, {txn, node}, Type::Void()},
            {"commit", commitKernel, {txn}, TupleType::fallible(program)},
            {"abort", abortKernel, {txn}, Type::Void()},
        };
    }();
    return ops;
}

} // namespace

const std::vector<oper_group_ptr_t> &txnGroups() {
    static const std::vector<oper_group_ptr_t> groups = [] {
        std::vector<oper_group_ptr_t> out;
        for (const TxnOp &op : txnOps()) {
            param_vec_t params;
            for (Type *type : op.params) {
                params.emplace_back(type, false);
            }
            out.push_back(OperatorGroup::create(
                op.name,
                {{std::string("graph:") + op.name,
                  std::make_unique<StaticFuncTypeResolver>(
                      FunctionType::create({}, params, op.result))}}));
        }
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
        return out;
    }();
    return kernels;
}

} // namespace camel::graphmod
