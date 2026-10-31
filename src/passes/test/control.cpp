// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Draft-only topology tests: no synthetic operator payload is executed or encoded. */

#include <algorithm>
#include "camel/passes/test/control.h"
#include "../opt/generic/rewrite.h"
#include "camel/runtime/control.h"
#include "camel/runtime/graph_txn.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
using namespace camel::runtime;

gc_node_ref_t node(GraphTransaction &transaction, std::initializer_list<gc_node_ref_t> predecessors = {}) {
    auto &draft = transaction.edit();
    return draft.addNode(
        DraftNodeInit{
            .kind       = GCNodeKind::Oper,
            .ctrlInputs = std::span<const gc_node_ref_t>(predecessors.begin(), predecessors.size()),
        });
}
void check(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void chainAndBudget() {
    auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
    auto &draft = owner->edit();
    const auto a = node(*owner), b = node(*owner, {a}), c = node(*owner, {b});
    const auto d = node(*owner, {a, c});
    check(!hasControlDependency(*owner, a, c, 0), "zero-budget query must be conservative");
    check(reduceControlDependencies(*owner, 0) == 0, "zero-budget reduction changed edges");
    check(reduceControlDependencies(*owner, 1) == 0, "incomplete search removed an unproven edge");
    const std::vector<gc_node_ref_t> before{a, b, c};
    const auto frontier = controlFrontier(*owner, before);
    check(frontier.size() == 1 && frontier.front() == c, "rewrite frontier retained ancestors");
    check(reduceControlDependencies(*owner) == 1, "transitive CTRL edge was not removed");
    check(
        draft.ctrlInputsOf(d).size() == 1 && draft.ctrlInputsOf(d).front() == c,
        "latest predecessor was not retained");
    check(!appendControlDependency(*owner, a, d), "rewrite reinserted an implied edge");
    check(reduceControlDependencies(*owner) == 0, "small graph reduction is not idempotent");
}
void mandatoryDataPaths() {
    auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
    auto &draft = owner->edit();
    const auto a = node(*owner), b = node(*owner);
    draft.setNormInputs(b, std::span<const gc_node_ref_t>(&a, 1));
    const auto c = node(*owner, {a, b});
    check(reduceControlDependencies(*owner) == 1, "mandatory data path was not used as proof");
    check(
        draft.ctrlInputsOf(c).size() == 1 && draft.ctrlInputsOf(c).front() == b,
        "data/control path lost its required predecessor");
    const auto d = node(*owner, {a});
    draft.setWithInputs(d, std::span<const gc_node_ref_t>(&b, 1));
    check(
        reduceControlDependencies(*owner) == 1 && draft.ctrlInputsOf(d).empty(),
        "context data path did not cover a single control");
    const auto e = node(*owner);
    draft.setNormInputs(e, std::span<const gc_node_ref_t>(&a, 1));
    check(!appendControlDependency(*owner, a, e), "rewrite added a direct data-covered control");
    const auto call = draft.addNode(
        DraftNodeInit{
            .kind       = GCNodeKind::Func,
            .normInputs = std::span<const gc_node_ref_t>(&a, 1),
        });
    check(
        appendControlDependency(*owner, a, call),
        "argument ordering needed by an imminent inline was skipped");
    // The explicit edge can be reduced on a stable graph, but not skipped before splicing.
    draft.setCtrlInputs(call, {});
    const auto gate = draft.addNode(
        DraftNodeInit{
            .kind       = GCNodeKind::Gate,
            .normInputs = std::span<const gc_node_ref_t>(&a, 1),
        });
    check(appendControlDependency(*owner, a, gate), "GATE lost its required first control");
    check(
        reduceControlDependencies(*owner) == 0 && draft.ctrlInputsOf(gate).size() == 1,
        "GATE lost its last control");
}
void conditionalDataIsNotMandatory() {
    auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
    auto &draft = owner->edit();
    const auto a = node(*owner), b = node(*owner);
    const gc_node_ref_t alternatives[] = {a, b};
    const auto join                    = draft.addNode(
        DraftNodeInit{
            .kind       = GCNodeKind::Join,
            .withInputs = alternatives,
        });
    const auto user = node(*owner, {a, join});
    draft.setNormInputs(user, std::span<const gc_node_ref_t>(&join, 1));
    check(reduceControlDependencies(*owner) == 1, "completed JOIN was not an endpoint proof");
    check(
        draft.ctrlInputsOf(user).size() == 1 && draft.ctrlInputsOf(user).front() == a,
        "an unselected JOIN arm was treated as mandatory");
}
void dataBudget() {
    auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
    auto &draft = owner->edit();
    const auto first = node(*owner);
    auto previous    = first;
    for (size_t i = 0; i < 300; ++i) {
        const auto current = node(*owner);
        draft.setNormInputs(current, std::span<const gc_node_ref_t>(&previous, 1));
        previous = current;
    }
    const auto last = node(*owner, {first});
    draft.setNormInputs(last, std::span<const gc_node_ref_t>(&previous, 1));
    check(
        reduceControlDependencies(*owner) == 0 && draft.ctrlInputsOf(last).size() == 1,
        "data search exceeded its per-target budget");
}
void branchBoundaries() {
    auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
    auto &draft = owner->edit();
    const auto a = node(*owner);
    DraftBrchPayload body{.armCount = 1};
    std::vector<std::byte> bytes(sizeof(body));
    std::memcpy(bytes.data(), &body, sizeof(body));
    const auto branch = draft.addNode(
        DraftNodeInit{
            .kind       = GCNodeKind::Brch,
            .payload    = bytes,
            .ctrlInputs = std::span<const gc_node_ref_t>(&a, 1),
        });
    const auto other = node(*owner, {a});
    const auto head  = node(*owner, {branch, a, other});
    draft.setNormInputs(head, std::span<const gc_node_ref_t>(&other, 1));
    const auto tail = node(*owner, {a, head});
    draft.setNormInputs(tail, std::span<const gc_node_ref_t>(&head, 1));
    (void)tail;
    (void)reduceControlDependencies(*owner);
    // The BRCH keeps what it waits on, and what waits on it keeps waiting.
    check(draft.ctrlInputsOf(branch).size() == 1, "the controls of a boundary were reduced");
    check(
        std::ranges::find(draft.ctrlInputsOf(head), branch) != draft.ctrlInputsOf(head).end(),
        "the wait on a BRCH was reduced");
}
void denseChain() {
    auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
    auto &draft = owner->edit();
    std::vector<gc_node_ref_t> predecessors;
    constexpr size_t count = 128;
    for (size_t i = 0; i < count; ++i) {
        const auto current = draft.addNode(
            DraftNodeInit{
                .kind       = GCNodeKind::Oper,
                .ctrlInputs = predecessors,
            });
        predecessors.push_back(current);
    }
    check(reduceControlDependencies(*owner) == 8001, "dense CTRL chain did not reduce to its spine");
    for (size_t i = 1; i < count; ++i) {
        check(
            draft.ctrlInputsOf(predecessors[i]).size() == 1 &&
                draft.ctrlInputsOf(predecessors[i]).front() == predecessors[i - 1],
            "dense chain lost its immediate predecessor");
    }
}
void denseDataChain() {
    auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
    auto &draft = owner->edit();
    std::vector<gc_node_ref_t> predecessors;
    for (size_t i = 0; i < 128; ++i) {
        const auto current = node(*owner);
        draft.setCtrlInputs(current, predecessors);
        if (!predecessors.empty()) {
            draft.setNormInputs(current, std::span<const gc_node_ref_t>(&predecessors.back(), 1));
        }
        predecessors.push_back(current);
    }
    check(reduceControlDependencies(*owner) == 8128, "dense data chain retained covered controls");
}
void forwardingGateConsumers() {
    using camel::passes::generic::bypassValueGate;
    auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
    auto &draft = owner->edit();
    const auto value = node(*owner), wait = node(*owner);
    const auto gate = draft.addGateNode(nullptr);
    draft.setNormInputs(gate, std::span(&value, 1));
    draft.setCtrlInputs(gate, std::span(&wait, 1));
    const auto reader = node(*owner), control = node(*owner, {gate});
    draft.setNormInputs(reader, std::span(&gate, 1));
    size_t budget = 0;
    check(!bypassValueGate(*owner, gate, budget), "zero budget bypassed a gate");
    budget = 64;
    check(bypassValueGate(*owner, gate, budget), "ordinary forwarding gate survived");
    check(
        !draft.header(gate) && draft.normInputsOf(reader).front() == value,
        "reader did not receive the original value");
    check(hasControlDependency(*owner, wait, reader), "reader lost its wait");
    check(
        hasControlDependency(*owner, wait, control) && hasControlDependency(*owner, value, control),
        "control-only consumer lost value completion or explicit wait");
    check(draft.ctrlInputsOf(value).empty(), "wait moved onto the value producer");

    const auto conditional = draft.addGateNode(nullptr);
    draft.setNormInputs(conditional, std::span(&value, 1));
    draft.setCtrlInputs(conditional, std::span(&wait, 1));
    const auto join = draft.addNode(DraftNodeInit{.kind = GCNodeKind::Join});
    draft.setWithInputs(join, std::span(&conditional, 1));
    check(!bypassValueGate(*owner, conditional, budget), "arm-local wait moved to JOIN");
    check(draft.ctrlInputsOf(join).empty(), "JOIN acquired an unconditional arm dependency");

    const auto barrier = draft.addGateNode(nullptr);
    draft.setNormInputs(barrier, std::span(&value, 1));
    std::vector<gc_node_ref_t> waits;
    for (size_t i = 0; i < 4; ++i)
        waits.push_back(node(*owner));
    draft.setCtrlInputs(barrier, waits);
    for (size_t i = 0; i < 4; ++i) {
        const auto user = node(*owner);
        draft.setNormInputs(user, std::span(&barrier, 1));
    }
    check(!bypassValueGate(*owner, barrier, budget), "shared barrier caused excess edge growth");

    const auto output = draft.addGateNode(nullptr);
    draft.setNormInputs(output, std::span(&value, 1));
    draft.setCtrlInputs(output, std::span(&wait, 1));
    const auto outputReader = node(*owner);
    draft.setNormInputs(outputReader, std::span(&output, 1));
    draft.setOutputNode(output);
    check(!bypassValueGate(*owner, output, budget), "output anchor lost its identity");
}
/// A node with many readers: the readers are added and erased in an order unlike the order they
/// were added in, as dead code is after inlining. Each step costs the reader, not the readers
/// so far; the plan's time limit is what fails a quadratic implementation.
void manyReaders() {
    auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
    auto &draft = owner->edit();
    const auto producer = node(*owner);
    constexpr size_t kReaders = 200000;
    std::vector<gc_node_ref_t> readers;
    readers.reserve(kReaders);
    for (size_t i = 0; i < kReaders; ++i) {
        const auto reader = node(*owner);
        draft.appendInput(DraftEdgeKind::Data, reader, producer);
        // Reading it twice does not make it two readers.
        if (i % 3 == 0) {
            draft.appendInput(DraftEdgeKind::Data, reader, producer);
        }
        readers.push_back(reader);
    }
    check(draft.normUsersOf(producer).size() == kReaders, "a reader was lost or counted twice");
    uint64_t state = 0x9E3779B97F4A7C15ull;
    for (size_t i = readers.size(); i > 1; --i) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        std::swap(readers[i - 1], readers[(state >> 33) % i]);
    }
    for (size_t i = 0; i < readers.size(); ++i) {
        if (i % 2 == 0) {
            draft.eraseNode(readers[i]);
        } else {
            while (draft.unlinkInput(DraftEdgeKind::Data, readers[i], producer)) {
            }
        }
        if (i % 50000 == 0) {
            check(
                draft.normUsersOf(producer).size() == kReaders - i - 1,
                "the readers do not add up");
        }
    }
    check(draft.normUsersOf(producer).empty(), "a reader was left behind");
}
/// The queries that ask what orders a node read the chain of a `sync` block. Its wait is the sync
/// edge the block wrote (docs/design/effects/edges.cn.md 1.5), so a gate whose only wait is
/// that edge still carries the program's order, and a walk that follows a node's orderings follows
/// the chain through a node that carries none. Both are what the generic rewrite pass asks before
/// it replaces a node (src/passes/opt/generic/rewrite.cpp, `carriesOrdering` and `orderedSources`).
void syncChainOrdering() {
    // A gate whose only wait is the chain's tail: one data port, one sync predecessor and no
    // control input at all (5.3).
    {
        auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
        auto &draft = owner->edit();
        const auto tail  = node(*owner);
        const auto value = node(*owner);
        const auto gate  = draft.addGateNode(camel::core::type::Type::Float32());
        draft.setNormInputs(gate, std::span<const gc_node_ref_t>(&value, 1));
        check(draft.setSyncPredecessor(gate, tail), "a gate did not take the chain's tail");
        const auto reader = node(*owner);
        draft.setNormInputs(reader, std::span<const gc_node_ref_t>(&gate, 1));
        const auto sources = camel::passes::generic::orderedSources(*owner, reader);
        check(
            sources.size() == 1 && sources.front() == gate,
            "a gate that waits only on the sync chain does not carry the order");
    }
    // A node that carries no order of its own: the walk reaches what ordered it through the chain
    // it stands in, not only through the control edge it used to be written as.
    {
        auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
        auto &draft = owner->edit();
        const auto tail  = node(*owner);
        const auto value = node(*owner);
        const auto plain = draft.addNode(DraftNodeInit{.kind = GCNodeKind::Copy});
        draft.setNormInputs(plain, std::span<const gc_node_ref_t>(&value, 1));
        check(draft.setSyncPredecessor(plain, tail), "a node did not take the chain's tail");
        const auto reader = node(*owner);
        draft.setNormInputs(reader, std::span<const gc_node_ref_t>(&plain, 1));
        const auto sources = camel::passes::generic::orderedSources(*owner, reader);
        check(
            std::ranges::find(sources, tail) != sources.end(),
            "the walk did not follow the sync chain");
    }
    // The order reduction reads the chain too: two order edges meet in one node and the chain
    // already orders one of the two ends before the other, so the edge that spells it says nothing
    // (docs/design/effects/edges.cn.md 5.4). Reading order edges alone, the pass finds nothing.
    {
        auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
        auto &draft = owner->edit();
        const auto head = node(*owner);
        const auto next = node(*owner);
        check(draft.setSyncPredecessor(next, head), "a node did not take a sync predecessor");
        const auto user             = node(*owner);
        const gc_node_ref_t ordered[] = {head, next};
        draft.setOrderInputs(user, ordered);
        check(reduceOrderDependencies(*owner) == 1, "the pass did not read the sync chain");
        check(
            draft.orderInputsOf(user).size() == 1 && draft.orderInputsOf(user).front() == next,
            "the pass dropped the order the chain does not give");
    }
    // The block above is what keeps the reduction from falling back to order edges alone, and the
    // way back is one line: move `walk(draft.syncUsersOf(from))` in src/runtime/control.cpp back
    // inside `if (readsEveryKind)` and this check fails with "the pass did not read the sync chain",
    // which turns `control.topology` and `control.test_pass_runs_when_named`
    // (test/plans/feat/opt/control.plan.toml) red -- 4 cases, 2 fail; restore the line and the plan
    // is 4 green. A check nobody has seen fail is a check nobody knows is load-bearing.
}
/// The two readings of one order reduction. A document is compared by the order a program holds,
/// so an ordering edge whose two ends a data path already orders says nothing and is dropped
/// (`reduceDocumentOrdering`, the canonical export). A graph a pass edits keeps it: the data path
/// there is a path the pass may still rewrite, so the edge is the conservative answer
/// (`reduceOrderDependencies`, `std::opt::order`). Both readings are one walk with one criterion;
/// what differs is which edges order and which are candidates.
void documentOrdering() {
    // An `order` edge a data path covers: the document drops it, the pass keeps it.
    {
        auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
        auto &draft = owner->edit();
        const auto read = node(*owner), step = node(*owner);
        draft.setNormInputs(step, std::span<const gc_node_ref_t>(&read, 1));
        const auto write = node(*owner);
        draft.setNormInputs(write, std::span<const gc_node_ref_t>(&step, 1));
        draft.setOrderInputs(write, std::span<const gc_node_ref_t>(&read, 1));
        check(reduceOrderDependencies(*owner) == 0, "the pass dropped a single order edge");
        check(reduceDocumentOrdering(*owner) == 1, "a data path did not cover an order edge");
        check(draft.orderInputsOf(write).empty(), "the covered order edge was kept");
    }
    // A `sync` edge a data path covers: a document reads the chain link the same way, and the pass
    // never considers a sync edge.
    {
        auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
        auto &draft = owner->edit();
        const auto first = node(*owner), step = node(*owner);
        draft.setNormInputs(step, std::span<const gc_node_ref_t>(&first, 1));
        const auto next = node(*owner);
        draft.setNormInputs(next, std::span<const gc_node_ref_t>(&step, 1));
        check(draft.setSyncPredecessor(next, first), "a node did not take a sync predecessor");
        check(reduceOrderDependencies(*owner) == 0, "the pass touched a sync edge");
        check(reduceDocumentOrdering(*owner) == 1, "a data path did not cover a sync edge");
        check(draft.syncInputsOf(next).empty(), "the covered sync edge was kept");
    }
    // What nothing implies stays, and a pair two kinds spell is one ordering rather than two: the
    // builder writes a control wait beside the sync link it makes, and neither covers the other.
    {
        auto owner = camel::runtime::GraphTransaction::open(static_cast<camel::runtime::GraphId>(1));
        auto &draft = owner->edit();
        const auto first = node(*owner), second = node(*owner, {first});
        check(draft.setSyncPredecessor(second, first), "a node did not take a sync predecessor");
        check(reduceDocumentOrdering(*owner) == 0, "a direct pair was reduced away");
        check(
            draft.syncInputsOf(second).size() == 1 && draft.ctrlInputsOf(second).size() == 1,
            "one pair lost one of the kinds that spell it");
    }
}
} // namespace

const camel::runtime::Program *
ControlTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    chainAndBudget();
    mandatoryDataPaths();
    conditionalDataIsNotMandatory();
    dataBudget();
    branchBoundaries();
    denseChain();
    denseDataChain();
    forwardingGateConsumers();
    manyReaders();
    syncChainOrdering();
    documentOrdering();
    os << "CONTROL topology, budgets, branches, dense chain: PASS\n";
    return program;
}
