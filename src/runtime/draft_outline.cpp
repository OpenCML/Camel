// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The reverse of `inline` at the draft level: a group of nodes becomes a graph of its own, and a call
// of it takes the place of the value the group handed on. `inline` (draft_inline.h) copies a callee
// into a graph; this copies a part of a graph into a callee, and
// `docs/design/graph-editing/README.cn.md` 7 and 15.3 name the two as each other's reverse.
//
// Three things are the whole of it:
//
// * **The boundary.** The values the group reads from outside are the new graph's parameters, and
//   the one value it hands on is its result. A group with more than one value out, or with an
//   ordering, a chain place or a context input on its boundary, is refused rather than guessed at:
//   that is what "边界明确、不含锚点、新图符合期望类型" (8.1) asks a commit to have been given.
// * **The graph.** It is made the way a rewrite makes one: `GraphTransaction::open` on an id the
//   context hands out, ports for the entries, the group's nodes copied in, the result as the exit,
//   and `encode`. Its signature states exactly the ports and the exit the body has, so the two
//   answers a graph publishes agree (`validateRuntimeGraphPortTypes`).
// * **The delivery.** The graph was in no program before this edit, so the commit would drop it in
//   silence: it is noted (`noteMadeGraph`) as the graphs this transaction made, which the closure
//   the commit delivers is seeded with (draft_session.cpp, `collectCommitClosure`).

#include "camel/runtime/draft_outline.h"

#include "camel/core/operator_table.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"
#include "camel/runtime/program.h"
#include "camel/utils/assert.h"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <unordered_map>

namespace camel::runtime {

namespace {

using camel::core::type::FunctionType;
using camel::core::type::FunctionTypeFactory;
using camel::core::type::Type;

[[noreturn]] void refuse(const std::string &why) { throw std::runtime_error(why); }

std::string nodeName(gc_node_ref_t id) { return std::format("node {}", static_cast<unsigned>(id)); }

/// The operator body of a draft node, or a refusal naming the node: an OPER node is the only kind a
/// group may hold today, because it is the only one whose whole content (its operator) is a value
/// this layer can copy. A cast, a copy, a constant and a call all carry something else with them.
const GCOperBody &operBodyOf(const GraphDraft &draft, gc_node_ref_t id) {
    const auto payload = draft.payloadOf(id);
    if (payload.size() < sizeof(GCOperBody)) {
        refuse(std::format("{} is not an operation node", nodeName(id)));
    }
    return *reinterpret_cast<const GCOperBody *>(payload.data());
}

/// Which way the graph names `id`, for the refusal: the answer `namesNode` gives, said out loud. A
/// group that breaks the boundary rule should be told which rule it broke, and "it anchors its graph"
/// alone leaves the caller looking for the anchor.
std::string namingOf(const GraphDraft &draft, gc_node_ref_t id) {
    const auto names = [id](std::span<const gc_node_ref_t> values) {
        return std::ranges::find(values, id) != values.end();
    };
    if (id == draft.entryNode()) {
        return "its entry";
    }
    if (id == draft.exitNode()) {
        return "its exit";
    }
    if (id == draft.outputNode()) {
        return "its output";
    }
    if (id == draft.returnNode()) {
        return "its return";
    }
    if (names(draft.normPorts())) {
        return "one of its normal ports";
    }
    if (names(draft.withPorts())) {
        return "one of its context ports";
    }
    if (names(draft.closureNodes())) {
        return "one of its closure nodes";
    }
    for (uint32_t index = 0; index < draft.regionCount(); ++index) {
        if (draft.region(index).result == id) {
            return std::format("region {}'s result", index);
        }
        if (names(draft.regionPortsOf(index))) {
            return std::format("one of region {}'s ports", index);
        }
    }
    return "a name this layer does not read";
}

/// The boundary a group of nodes has: the values it reads from outside, in the order a port is made
/// of each, and the one value it hands on. A group whose boundary is not that shape is refused with
/// the rule it breaks rather than guessed at (an empty group, a node the transaction does not hold,
/// a node that anchors its graph, a node that carries an access or an ordering, no value out or more
/// than one).
///
/// It is this file's reader, not a declaration in `draft_outline.h`: it takes the draft, and its one
/// caller is `outlineDraftNodes` below, which already has the draft through the transaction it was
/// given. Written into the header it would be interface surface that nothing outside this file can
/// call, and it would put the type's name in a header that otherwise never names it.
struct DraftOutlineBoundary {
    std::vector<gc_node_ref_t> entries;
    gc_node_ref_t result = kInvalidNodeRef;
};

DraftOutlineBoundary draftOutlineBoundary(const GraphDraft &draft, std::span<const gc_node_ref_t> group) {
    if (group.empty()) {
        refuse("the group is empty");
    }
    const auto inGroup = [&](gc_node_ref_t id) {
        return std::ranges::find(group, id) != group.end();
    };
    DraftOutlineBoundary boundary;
    std::vector<gc_node_ref_t> members(group.begin(), group.end());
    std::ranges::sort(members);
    for (gc_node_ref_t id : members) {
        const DraftNodeHeader *header = draft.header(id);
        if (header == nullptr || !draft.alive(id)) {
            refuse(std::format("{} is not a node the transaction holds", nodeName(id)));
        }
        // A node the graph names by something other than a read is not a group's part (design 8.1,
        // "不含锚点"): the exit, the output, the return value, the entry, a port, a capture, a
        // region's result or a control region's port. `namesNode` is the one reading of that rule
        // (`eraseUnreadNodes` asks it too), so the two cannot disagree about which nodes a group may
        // lose.
        if (draft.namesNode(id)) {
            refuse(std::format(
                "{} is {} and the graph names it: a group holds what the graph does not name (design "
                "8.1, '不含锚点')",
                nodeName(id),
                namingOf(draft, id)));
        }
        if (header->kind != GCNodeKind::Oper) {
            refuse(std::format(
                "{} is not an operation: a group holds operations only today, so that what the "
                "graph copies is the operator and nothing else", nodeName(id)));
        }
        const GCOperBody &body = operBodyOf(draft, id);
        // "移出区域的必须是纯且不会出故障的计算" is 9.7's rule for hoisting, and the wider one holds
        // here too: a group that carries an access would owe the commit the effect and order
        // questions of the region it lands in (8.1's ⑥), which this landing does not answer.
        if (!camel::core::OperatorTable::instance().isPure(body.uri())) {
            refuse(std::format(
                "{} carries an access: '{}' is not a pure operator, so the group is not outlined "
                "yet",
                nodeName(id),
                body.uri()));
        }
        for (const gc_node_ref_t pred : draft.ctrlInputsOf(id)) {
            if (!inGroup(pred)) {
                refuse(std::format(
                    "{} stands in a chain: {} is read before it, so the group has no place to "
                    "become a call of its own",
                    nodeName(id),
                    nodeName(pred)));
            }
        }
        for (const gc_node_ref_t pred : draft.orderInputsOf(id)) {
            if (!inGroup(pred)) {
                refuse(std::format(
                    "{} carries an order edge from {}: a group is outlined without one",
                    nodeName(id),
                    nodeName(pred)));
            }
        }
        for (const gc_node_ref_t pred : draft.syncInputsOf(id)) {
            if (!inGroup(pred)) {
                refuse(std::format(
                    "{} is written after {} in a sync block: a group is outlined without a chain "
                    "place",
                    nodeName(id),
                    nodeName(pred)));
            }
        }
        if (!draft.withInputsOf(id).empty()) {
            refuse(std::format(
                "{} has a context input: the new graph has ordinary parameters only",
                nodeName(id)));
        }
        for (const gc_node_ref_t pred : draft.normInputsOf(id)) {
            if (inGroup(pred)) {
                continue;
            }
            if (std::ranges::find(boundary.entries, pred) == boundary.entries.end()) {
                boundary.entries.push_back(pred);
            }
        }
    }
    // What the group hands on: the one value of it that something outside the group reads. A group
    // whose value nobody reads computes nothing that leaves it, and a group whose values several
    // readers outside take hands on more than one thing, which is a tuple the operator does not
    // write yet.
    for (const gc_node_ref_t id : members) {
        bool readOutside = false;
        for (const gc_node_ref_t user : draft.normUsersOf(id)) {
            readOutside = readOutside || !inGroup(user);
        }
        for (const gc_node_ref_t user : draft.withUsersOf(id)) {
            readOutside = readOutside || !inGroup(user);
        }
        if (!readOutside) {
            continue;
        }
        if (boundary.result != kInvalidNodeRef) {
            refuse(std::format(
                "the group hands on two values ({} and {}): a group is outlined with one result",
                nodeName(boundary.result),
                nodeName(id)));
        }
        boundary.result = id;
    }
    if (boundary.result == kInvalidNodeRef) {
        refuse("nothing outside the group reads what it computes");
    }
    return boundary;
}

} // namespace

DraftOutlineResult outlineDraftNodes(
    RuntimeGraphDraftSession &session, GraphTransaction &transaction,
    std::span<const gc_node_ref_t> group, std::string_view name) {
    GraphDraft &draft              = transaction.edit();
    const DraftOutlineBoundary boundary = draftOutlineBoundary(draft, group);
    const DraftNodeHeader *const resultHeader = draft.header(boundary.result);
    ASSERT(resultHeader != nullptr && resultHeader->dataType != nullptr,
           "An outline result names a node with a type.");

    // The signature the group's boundary states: one ordinary parameter per entry, in the order the
    // entries were found, and the value the group hands on as the exit type.
    FunctionTypeFactory factory;
    for (const gc_node_ref_t entry : boundary.entries) {
        const DraftNodeHeader *const header = draft.header(entry);
        if (header == nullptr || header->dataType == nullptr) {
            refuse(std::format("{} has no type to be a parameter of", nodeName(entry)));
        }
        factory.addNormArg(std::string(), header->dataType, false);
    }
    factory.setExitType(resultHeader->dataType);
    FunctionType *const signature = factory.build();

    // The graph the group becomes: a new one, on an id the context hands out, built in a transaction
    // of its own that ends when this function does (`encode` is the last thing asked of it).
    auto madeTransaction = GraphTransaction::open(session.context()->graphIds().allocate(), signature);
    GraphDraft &made     = madeTransaction->edit();
    made.setProgram(&session.work());

    std::unordered_map<gc_node_ref_t, gc_node_ref_t> portOf;
    for (const gc_node_ref_t entry : boundary.entries) {
        Type *const type         = draft.header(entry)->dataType;
        const gc_node_ref_t port = made.addPortNode(type, made.allocateRuntimeSlot(type));
        made.appendNormPort(port);
        portOf.emplace(entry, port);
    }

    std::vector<gc_node_ref_t> members(group.begin(), group.end());
    std::ranges::sort(members);
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> copyOf;
    for (const gc_node_ref_t id : members) {
        const DraftNodeHeader *const header = draft.header(id);
        const GCOperBody &body              = operBodyOf(draft, id);
        // The frame slot is the new draft's: a data index is a position in the graph it was made in.
        const gc_node_ref_t copy = made.addOperNode(header->dataType, body.op, body.uri());
        made.setNodeRegion(copy, 0);
        made.setNodeOrigin(copy, header->origin);
        made.setNodePermissions(copy, header->permissions);
        made.setNodeAttrs(copy, header->attrs);
        made.setNodeRuntimeFlags(copy, header->runtimeFlags);
        copyOf.emplace(id, copy);
    }
    for (const gc_node_ref_t id : members) {
        std::vector<gc_node_ref_t> inputs;
        for (const gc_node_ref_t pred : draft.normInputsOf(id)) {
            if (const auto copied = copyOf.find(pred); copied != copyOf.end()) {
                inputs.push_back(copied->second);
                continue;
            }
            const auto port = portOf.find(pred);
            ASSERT(port != portOf.end(), "An outline entry is a port of the graph it becomes.");
            inputs.push_back(port->second);
        }
        made.setNormInputs(copyOf.at(id), inputs);
    }
    // The three answers the graph gives about the value it hands back, and they are three: the exit
    // is where a run of it ends, the output is what it hands on, and the return is how -- `Self`,
    // the node that holds the value is the value returned, which is what a function's body does
    // (`src/passes/test/resources.cpp` builds a synthesized function the same way). A graph with an
    // exit and no return node is a graph a call cannot take a value out of: std::fvm refuses to
    // compile it ("requires a value return node") and the node VM answers nothing through it, which
    // is a call whose value reads as 0.
    made.setExitNode(copyOf.at(boundary.result));
    made.setOutputNode(copyOf.at(boundary.result));
    made.setReturnNode(copyOf.at(boundary.result), GCReturnKind::Self);
    GCGraph *const encoded = made.encode(std::string(name), std::string(name));
    // The graph was in no program before this edit: the commit delivers it because the session notes
    // it, and not because a draft reaches it (design 2.5, `collectCommitClosure`).
    session.noteMadeGraph(encoded);

    // The place the group had: a call of the graph it became, reading what the group read, and read
    // by what read the group's value. It takes the result's origin and region, so a report about the
    // code the group was now points at the call that stands there.
    const gc_node_ref_t call = draft.addFuncNode(encoded->id(), resultHeader->dataType);
    draft.setNodeRegion(call, draft.nodeRegion(boundary.result));
    draft.setNodeOrigin(call, draft.nodeOrigin(boundary.result));
    draft.setNormInputs(call, boundary.entries);
    draft.replaceAllValueUses(boundary.result, call);
    // The call reads what the group read and is read by what read the group's value, and both of those
    // are reads. A place in the statement chain is not a read: a member that stands in one (a
    // statement, not a value) hands it to the call, so the chain reaches the call where it reached
    // the group. The group is one statement's worth of code, so at most one member has such a place,
    // and a group with more is refused rather than guessed at. A group of values has no chain place at
    // all: the call there runs because what reads it runs, the execution order walking back from the
    // exit along data ports (computeExecutionOrder), which is also why the call is given no place of
    // its own when there is none to take.
    std::vector<gc_node_ref_t> places;
    for (const gc_node_ref_t id : members) {
        if (occupiesChainPlace(draft, id)) {
            places.push_back(id);
        }
    }
    if (places.size() > 1) {
        refuse(std::format(
            "the group stands in {} places in a statement chain: outline the statements it holds "
            "one at a time",
            places.size()));
    }
    if (!places.empty()) {
        draft.takeStatementPlace(places.front(), call);
    }
    // The group's own nodes are the copy's source and nothing else reads them now. Erased, so that
    // the graph this edit leaves has one copy of the code and not two: `eraseUnreadNodes` refuses as
    // a set anything the graph names, which the boundary check above already ruled out, and anything
    // a node outside the set reads, which would have been a second value the group handed on.
    if (!draft.eraseUnreadNodes(group)) {
        refuse("the group's own nodes can not be taken out of the graph after the call replaced "
               "them");
    }

    return DraftOutlineResult{
        .graph   = encoded,
        .call    = call,
        .entries = boundary.entries,
    };
}

} // namespace camel::runtime