// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The execution plan of a graph version (docs/design/graph-store 4.6).
 *
 * What every back end needs to run a graph and used to compute for itself: the order its live
 * nodes run in, where each arm of a branch starts, the callee and the argument slots of each
 * direct call, which calls are in tail position. It is computed once, the first time a back end
 * asks (GCGraph::execPlan), from the frozen graph alone, and kept with the graph version: a
 * rewrite makes a new version, which gets a new plan. Only the nodes the exit depends on are in
 * it. The order is deterministic: the same graph gives the same plan in every run.
 *
 * The plan does not change after it is built, except for the record FastVM keeps in it about the
 * code it made for the version (`fastVm`).
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/rtdata/foreign.h"
#include "camel/runtime/graph.h"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

/// A node of the plan with what a back end reads of it while running, resolved once: the record,
/// the input runs, the payload and the arms of a BRCH. They point into the frozen graph, which
/// does not move, so running a node does not look anything up in the graph.
struct ExecNode {
    gc_node_ref_t ref         = kInvalidNodeRef;
    const GCNode *node        = nullptr;
    const gc_node_ref_t *norm = nullptr;
    const gc_node_ref_t *with = nullptr;
    const std::byte *body     = nullptr; // the payload; null when the node has none
    uint32_t normCount        = 0;
    uint32_t withCount        = 0;
    uint32_t armCount         = 0; // BRCH only

    std::span<const gc_node_ref_t> normInputs() const { return {norm, normCount}; }
    std::span<const gc_node_ref_t> withInputs() const { return {with, withCount}; }
    template <typename Body> const Body *bodyAs() const {
        return reinterpret_cast<const Body *>(body);
    }
};

/// A LOOP node whose condition and body are regions of the graph, as a back end runs it in the
/// frame of the graph (docs/technical/39_loops.md). In `topo` the nodes of the condition are one
/// range, those of the body the next, and the LOOP node follows them: [begin, condEnd) is the
/// condition, [condEnd, position) the body. A round is the condition, the test of the round
/// (at `condEnd`), the body, and the LOOP node, which takes the result of the body as the state
/// and starts the next round at `begin`. Slots are data slots of the frame.
struct ExecLoop {
    uint32_t position        = 0; // the topo index of the LOOP node
    uint32_t begin           = 0;
    uint32_t condEnd         = 0;
    uint32_t stateCount      = 0;     // k
    uint32_t collectionCount = 0;     // s
    bool iterates            = false; // for, for ... while: one element a round
    bool hasCond             = false; // while, for ... while
    /// Where the elements come from (camel::core::LoopIterableForm). `false` (Array): the slot of
    /// `iterableSlot` holds the array, and the round takes its element at the round number.
    /// `true` (Interval): Norm input 0 is the pair of the interval's two bounds, and the round
    /// takes `intervalFrom + round` while it is below `intervalTo`; no array of the interval
    /// exists (`for i in a..b`: the compiler fuses it, design principles 4.2).
    bool intervalIterates = false;
    /// One bound of an interval iterable: the integer the rounds are taken from, and where the
    /// plan has it. A bound the graph computes is read from its slot in the frame; one the graph
    /// holds as a constant is that constant (the pair of bounds is a value, and a pass may write
    /// it, or one of its two integers, as a constant instead of the FILL).
    struct IntervalBound {
        gc_data_idx_t slot = 0; // the frame slot of the bound, when !isConstant
        int64_t value      = 0; // the constant, when isConstant
        bool isConstant    = false;
    };
    gc_data_idx_t stateSlot    = 0; // the slot of the LOOP node: the state, a tuple if it wraps
    gc_data_idx_t iterableSlot = 0;
    /// The two bounds of an interval iterable (intervalIterates): the integers `from` and `to` of
    /// the half-open interval [from, to).
    IntervalBound intervalFrom;
    IntervalBound intervalTo;
    gc_data_idx_t condResultSlot = 0;
    gc_data_idx_t bodyResultSlot = 0;
    /// The slot the rounds are counted in: the number of rounds done while the loop runs, -1 once
    /// its test has stopped it.
    gc_data_idx_t roundSlot = 0;
    /// loopSlots[slotsOffset ..< +k] the slots of the first values of the state, then k of the
    /// condition's ports (when hasCond), then the body's ports: the element first (when it
    /// iterates), then k.
    uint32_t slotsOffset       = 0;
    const gc_data_idx_t *slots = nullptr; // &loopSlots[slotsOffset]
    /// The value type of the node when it wraps its state (k > 1 or it collects); else null.
    const camel::core::type::TupleType *valueType = nullptr;
    /// The state of a loop of several values kept as its k components instead of a tuple made
    /// again every round: true when the body ends in a tuple FILL that nothing but the LOOP node
    /// reads and every component is a value the body itself computes (exec_plan.cpp,
    /// `planStateComponents`). Then stateSlots[j] is the frame slot of component j: the body's
    /// own slot for it, so a round writes the next state in place and no tuple is allocated; the
    /// loop's value tuple is made when the loop starts and brought up to date when it ends.
    bool stateComponents            = false;
    uint32_t stateSlotsOffset       = 0;
    const gc_data_idx_t *stateSlots = nullptr; // k slots, by component (stateComponents only)
    uint64_t origin                 = 0;
};

/// What the memory plan decided about an update node's storage: an "returns a new value" update
/// (a path assignment, a rebinding like `xs = xs->sort`) either reuses the storage of the value it
/// is given or copies, and the decision is made once, here, from the graph
/// (docs/design/arrays-memory/README.cn.md 4.3.1). Every back end runs the node the plan names for
/// it (ExecPlan::operatorUriFor) instead of deciding for itself.
enum class StorageReuse : uint8_t {
    /// Not an update node: the plan decided nothing about it.
    None = 0,
    /// The plan proved the old value has no other observer -- the cell holds a value this same
    /// variable's own update made, and nothing read the cell since: the update reuses the input's
    /// storage.
    Input = 1,
    /// The cell escapes (a `var` parameter's or a captured cell): the plan cannot prove it here,
    /// so the update asks the array itself at run time (Array::canWriteInPlace): the only
    /// reference to a block it owns is written in place, anything else is copied.
    ByArrayCount = 2,
    /// The old value has an observer the plan can see (a read of the cell that keeps the value),
    /// or its origin is unknown: the update copies.
    Copy = 3,
};

/// What a back end does at a position of the plan for a loop that starts or goes on there.
enum class ExecLoopEvent : uint8_t {
    Begin, // the loop is entered: the first state; the condition's ports
    Test,  // the condition (if any) has run: stop, or bind the ports of the body
    End,   // the LOOP node: take the next state and go back to `begin`, or leave
};

class ExecPlan {
  public:
    /// The live nodes in the order they run (every node after its inputs; an arm of a branch is
    /// one range between the BRCH and its JOIN).
    std::vector<gc_node_ref_t> topo;
    /// The nodes of `topo`, in the same order. Built the first time they are asked for (only a
    /// back end that walks the nodes while it runs needs them), then kept.
    const std::vector<ExecNode> &nodes() const {
        if (nodesReady_.load(std::memory_order_acquire)) [[likely]] {
            return nodes_;
        }
        return buildNodes();
    }
    /// The position in `topo` of each node, by node reference; kNotInPlan for a node that does
    /// not run (dead).
    static constexpr uint32_t kNotInPlan = UINT32_MAX;
    std::vector<uint32_t> topoIndexByRef;
    /// The data slot of each node, by node reference.
    std::vector<gc_data_idx_t> dataIndexByRef;

    /// Per topo index: the callee of a FUNC node, by id (kInvalidGraphId for other nodes). The
    /// version of the callee is the one the program the graph is run in holds
    /// (Program::graph): a version shared by several programs has callees that differ.
    std::vector<GraphId> directCallTargetIds;
    /// Per topo index: the FUNC node's value is the graph's result and nothing but forwarding
    /// follows it (a tail call), or it feeds a tail JOIN through forwarding.
    std::vector<uint8_t> directCallTailEligible;
    std::vector<uint8_t> directCallFeedsTailJoin;
    /// The data slots of the arguments of the FUNC node at topo index i (norm, then with):
    /// directCallArgSlots[directCallArgOffsets[i] ..< directCallArgOffsets[i + 1]].
    std::vector<uint32_t> directCallArgOffsets;
    std::vector<gc_data_idx_t> directCallArgSlots;
    /// For the BRCH at topo index i, branchArmEntries[branchArmEntryOffsets[i] + arm] is the node
    /// where that arm starts executing: its first node in `topo` (whatever that node does; the
    /// events of a loop that starts there run before it), or the JOIN when the arm has nothing to
    /// execute.
    std::vector<uint32_t> branchArmEntryOffsets;
    std::vector<gc_node_ref_t> branchArmEntries;
    /// The same for where the arm ends: the last node of the arm in `topo`; the JOIN when the arm
    /// has nothing to execute.
    std::vector<gc_node_ref_t> branchArmTails;
    /// The node whose value is the result of the graph (see resolveRuntimeTailValueRef); invalid
    /// when the graph returns nothing or the value is not a slot.
    gc_node_ref_t tailValueRef = kInvalidNodeRef;
    size_t tailValueTopoIndex  = 0;
    bool tailValueIsJoin       = false;
    /// The tail value is the last thing the graph does but forwarding GATEs: a call that is the
    /// tail value, or feeds a tail JOIN through forwarding, replaces the rest of the graph (a
    /// tail call). This is the one rule every back end uses.
    bool tailAnchorOk = false;
    /// The FUNC nodes in tail position (by node reference, sorted): the tail value itself and the
    /// calls that feed a tail JOIN.
    std::vector<gc_node_ref_t> tailCalls;
    /// The JOIN nodes in tail position: the tail value when it is a JOIN, and every JOIN whose
    /// value it yields through forwarding GATEs.
    std::vector<gc_node_ref_t> tailJoins;
    bool isTailCall(gc_node_ref_t ref) const {
        return std::binary_search(tailCalls.begin(), tailCalls.end(), ref);
    }
    /// The data slots of the ports a caller binds, norm ports then with ports.
    std::vector<gc_data_idx_t> calleePortSlots;

    /// The loops of the graph that are regions of it. loopEventOffsets[i] ..< loopEventOffsets[i
    /// + 1] are the events at topo index i, each `loop index * 3 + ExecLoopEvent`, the outer
    /// loop's first; both are empty when the graph has no such loop.
    std::vector<ExecLoop> loops;
    std::vector<gc_data_idx_t> loopSlots;
    /// By topo index: the node is the body's tuple FILL of a loop that keeps its state as its
    /// components (ExecLoop::stateComponents). The FILL builds a tuple nothing reads — the loop
    /// takes the components themselves and makes its value tuple at its start and end — so a
    /// back end that walks the plan does not run it. Empty when no loop of the graph does this.
    std::vector<uint8_t> elidedAt;
    std::vector<uint32_t> loopEventOffsets;
    std::vector<uint32_t> loopEvents;
    std::span<const uint32_t> loopEventsAt(size_t topoIndex) const {
        return {
            loopEvents.data() + loopEventOffsets[topoIndex],
            loopEventOffsets[topoIndex + 1] - loopEventOffsets[topoIndex]};
    }

    /// The function of each OPER node, by node reference, once a back end has resolved it; null
    /// until then (resolving an operator can import a module, and an unknown one is a fault of
    /// the run, not of the plan). Written by the back ends that run the graph node by node.
    mutable std::unique_ptr<std::atomic<operator_t>[]> resolvedOpers;

    /// ---- In-place updates: where the decision lives
    /// (docs/design/arrays-memory/README.cn.md 4.3.1) -------------------
    //
    // An update node is an OPER node that names the in-place form of an operator and takes its
    // array from `:var/take` of a variable's cell (Builder::pathAssignmentOf,
    // Builder::inPlaceRebindingOf). The plan decides, once, whether the update reuses the storage
    // of the value it is given (StorageReuse), and every back end follows: a node the plan marks
    // Copy runs the plain form of the operator (operatorUriFor), which copies; the two forms that
    // reuse the storage run the in-place form, which asks the array itself where the plan could
    // not prove it (StorageReuse::ByArrayCount).
    std::vector<StorageReuse> storageReuseAt; // by topo index; None for every other node
    StorageReuse storageReuseOf(gc_node_ref_t ref) const {
        const size_t index = ref < topoIndexByRef.size() ? topoIndexByRef[ref] : kNotInPlan;
        return index == kNotInPlan || index >= storageReuseAt.size() ? StorageReuse::None
                                                                     : storageReuseAt[index];
    }
    /// How many update nodes the plan decided each way.
    size_t storageReuseCount(StorageReuse decision) const;

    /// ---- The references that come back: route 4's plan half, described and not executed ------
    //
    // Design principles 10.5: a value is freed by counting alone, the count equals the number of
    // its holders, and the count comes from one last-use analysis on the execution plan that every
    // back end follows. What follows is that analysis's output. It is laid out from the "position
    // of the last use" column below, and no back end executes it yet: the only place a counted
    // block is released is ForeignResourceObject::finalize, inside a collection (see
    // docs/handover/reports/2026-11-15-count-at-zero-has-no-entry.cn.md, and the block's lifetime
    // in the note on `ForeignResourceControlBlock::releaseCountNow`).
    //
    // The rule is the author's ruling of 2026-11-15 (docs/handover/reviews/
    // 2026-11-15-who-gives-back-the-reference-a-counted-value-is-made-with.cn.md): the output of a
    // value node is held by each of its uses, the last use takes the producing reference over, and
    // every other use is a copy. Said in references: a value is born with one (its producer's),
    // takes `copies` more (one per copy), and one comes back after each use -- so
    // `1 + copies = giveBacks + kept`, where `kept` is 1 when the program itself keeps the value
    // (it is the graph's result): then the surviving reference is the caller's.
    //
    // A node that only forwards a value (a JOIN hands on the arm's value, a GATE passes its input
    // through, a port hands on what it was given) produces nothing of its own: its uses are the
    // uses of the values it carries, so the arm whose value the JOIN hands on is the one whose
    // reference comes back, whichever arm runs.
    //
    // A use that *packs* the value into another value -- a FILL builds a closure tuple or an
    // aggregate out of the values it is given -- does not end when the FILL has run: the packing
    // value holds that reference until it dies itself. So the reference comes back where the
    // packing value's last reference dies, not at the FILL's position (`packed` marks those rows).
    // This is the class that made the half count of 2026-11-15 release live boxes: a closure
    // captured a handle, the slot that made it was cleared, and the count reached zero while the
    // closure still held it (docs/handover/reports/2026-11-15-the-refs-that-come-back.cn.md 3).
    //
    // What this table does not decide, and why nothing executes it: a value a callee keeps, or one
    // a later graph reads, is a holder no edge of this graph shows. Counting those is the rest of
    // the way to route 4.
    struct RefGiveBack {
        gc_node_ref_t value = kInvalidNodeRef; // the value node the reference belongs to
        gc_node_ref_t at    = kInvalidNodeRef; // the node that has run when it comes back
        uint32_t count      = 0;               // how many references come back there (1 today)
        /// The reference comes back because the value that packed it in has died, not because a use
        /// of this value ended.
        bool packed = false;
    };
    /// By topo index: the node makes the value the plan counts (it is neither a port, nor a
    /// constant, nor a node that only forwards).
    std::vector<uint8_t> refProducedAt;
    /// By topo index: how many references the node's output takes beyond the producing one.
    std::vector<uint32_t> refCopiesAt;
    /// By topo index: how many references come back when this node has run. The same schedule as
    /// `refGiveBacks`, keyed the way an instruction is: a back end that emits a count at a position
    /// reads this, and the sum over the plan is `refGiveBacksTotal()`.
    std::vector<uint32_t> refGiveBackAt;
    /// By topo index: the program keeps the node's value (it is the graph's result).
    std::vector<uint8_t> refKeptAt;
    /// By topo index: refGiveBacks[refGiveBackOffsets[i] ..< refGiveBackOffsets[i + 1]] come back
    /// once the node at that position has run, in the order the producing values are laid out.
    std::vector<uint32_t> refGiveBackOffsets;
    std::vector<RefGiveBack> refGiveBacks;
    std::span<const RefGiveBack> refGiveBacksAt(size_t topoIndex) const {
        if (topoIndex + 1 >= refGiveBackOffsets.size()) {
            return {};
        }
        return {
            refGiveBacks.data() + refGiveBackOffsets[topoIndex],
            refGiveBackOffsets[topoIndex + 1] - refGiveBackOffsets[topoIndex]};
    }
    /// The totals the balance `produced + copies = giveBacks + kept + packed` is read from: the
    /// rows that come back because a use ended, and the rows that come back because the value that
    /// packed them in died.
    size_t refProducedTotal() const;
    size_t refCopiesTotal() const;
    size_t refGiveBacksTotal() const;
    size_t refKeptTotal() const;
    size_t refPackedTotal() const;
    /// Whether the balance holds: every reference a value is born with is either given back by the
    /// plan or kept by the program. False would mean this code and the rule disagree.
    bool refBalanceHolds() const;

    /// ---- The position of the last use: route 4's column -------------------------------------
    //
    // The author's ruling of 2026-11-15 puts the last use on the walk that already decides storage
    // reuse -- it "carries the execution order and who reads what" -- and says it is computed there
    // and not written a second time. So `planStorageReuse` fills this column, and the references
    // that come back are placed from it: a value is never read again after `lastUseAt`, which is
    // the moment its producing reference is handed over (unless the program keeps it).
    //
    // What it answers, for one value: after which position in the plan is this value never read
    // again. `kNoLastUse` is the honest answer for a value nothing reads -- such a value's
    // producing reference comes back at its own position.
    static constexpr uint32_t kNoLastUse = 0xFFFFFFFFu;
    std::vector<uint32_t> lastUseAt; // by topo index; kNoLastUse for a value nothing reads
    uint32_t lastUseOf(gc_node_ref_t ref) const {
        const size_t index = ref < topoIndexByRef.size() ? topoIndexByRef[ref] : kNotInPlan;
        return index == kNotInPlan || index >= lastUseAt.size() ? kNoLastUse : lastUseAt[index];
    }

    /// ---- The entries: the holds and the give-backs, as one list per position -----------------
    //
    // `refCopiesAt` and `refGiveBackAt` are the same schedule counted; this is the schedule as the
    // entries a back end reads, one row per thing to do, so that "what the plan says" and "what the
    // count does" can be compared row by row rather than by two totals that happen to agree. A
    // `Retain` at a position is the node's output taking the holds beyond its own; a `GiveBack` is
    // a reference coming back once that position has run.
    enum class RefEntryKind : uint8_t {
        Retain   = 0,
        GiveBack = 1,
    };
    struct RefEntry {
        RefEntryKind kind   = RefEntryKind::GiveBack;
        gc_node_ref_t value = kInvalidNodeRef; // the value the reference belongs to
        uint32_t count      = 0;
        bool packed         = false; // a give-back that comes due when the packing value dies
    };
    std::vector<uint32_t> refEntryOffsets;
    std::vector<RefEntry> refEntries;
    std::span<const RefEntry> refEntriesAt(size_t topoIndex) const;
    /// How many entries of each kind the plan holds, which is the balance read row by row:
    /// `produced + retains == giveBacks + kept`.
    size_t refEntriesOfKind(RefEntryKind kind) const;
    bool refEntriesBalance() const;

    /// The operator a back end runs for the OPER node `ref`: the URI the node names, or -- for an
    /// update node the plan marked Copy -- the plain (copying) form of the in-place form it names
    /// (OperatorTable::plainForm). The text lives in the operator table, so the view stays valid.
    std::string_view operatorUriFor(gc_node_ref_t ref) const;
    /// What FastVM records about the code it made for this version: where its bytecode starts and
    /// how long it is (each stored plus one; 0 is "not compiled"), the JIT entry, flags, and how
    /// often an indirect call from it was made. Only FastVM reads and writes it.
    struct FastVmState {
        uintptr_t entryPc           = 0;
        uintptr_t graphLength       = 0;
        uintptr_t jitEntry          = 0;
        uintptr_t flags             = 0;
        uintptr_t indirectCallCount = 0;
        /// Keeps the bytecode range of the version in the interpreter's code space; gives it
        /// back when the version is freed (the plan is freed with it).
        std::shared_ptr<void> codeLease;
    };
    mutable FastVmState fastVm;

    /// The anchor's side table, kept with the plan for the reason `fastVm` is: the compiler knows
    /// what the run needs later, and the run reads it while it runs. The anchor's pc form -- a back
    /// end that runs bytecode holds a pc where the node form holds a node -- asks which node an
    /// instruction belongs to at the moment a counted box is made, which is after the compile that
    /// knew the answer is over. So the table lives here, with the plan it describes, and lasts
    /// exactly as long as the graph version that owns it.
    ///
    /// Empty unless `--gc-check` asked for the counted blocks: a compile that is not asked fills
    /// nothing, allocates nothing for it, and every reader of it finds an empty table.
    ///
    /// `nodeAtPc` is keyed by the graph's own pc numbering as the compiler emitted it, and the
    /// optimizer moves those keys with the instructions, in the same call that moves the origin
    /// table (`BytecodeOptimizer::optimize`), so a pc an interpreter holds in hand finds the row of
    /// the instruction it is pointing at and never the row of one that used to be there.
    /// `entryPc` is where that numbering starts in the image the run executes, so an image pc can
    /// be brought back to a key of `nodeAtPc`.
    struct CountedSites {
        std::unordered_map<size_t, gc_node_ref_t> nodeAtPc;
        size_t entryPc = 0;
    };
    mutable CountedSites countedSites;

    /// The graph version the plan was built from.
    const GCGraph *graph = nullptr;

    std::span<const gc_data_idx_t> directCallArgSlotsOf(size_t topoIndex) const {
        return {
            directCallArgSlots.data() + directCallArgOffsets[topoIndex],
            directCallArgOffsets[topoIndex + 1] - directCallArgOffsets[topoIndex]};
    }
    /// Where each arm of the BRCH `brch` starts executing (see branchArmEntries).
    std::span<const gc_node_ref_t> branchArmEntriesOfNode(gc_node_ref_t brch) const {
        return branchArmEntriesOf(topoIndexByRef[brch]);
    }
    std::span<const gc_node_ref_t> branchArmEntriesOf(size_t topoIndex) const {
        return {
            branchArmEntries.data() + branchArmEntryOffsets[topoIndex],
            branchArmEntryOffsets[topoIndex + 1] - branchArmEntryOffsets[topoIndex]};
    }
    /// Where each arm of the BRCH `brch` ends (see branchArmTails).
    std::span<const gc_node_ref_t> branchArmTailsOfNode(gc_node_ref_t brch) const {
        return branchArmTailsOf(topoIndexByRef[brch]);
    }
    std::span<const gc_node_ref_t> branchArmTailsOf(size_t topoIndex) const {
        return {
            branchArmTails.data() + branchArmEntryOffsets[topoIndex],
            branchArmEntryOffsets[topoIndex + 1] - branchArmEntryOffsets[topoIndex]};
    }

  private:
    const std::vector<ExecNode> &buildNodes() const;

    mutable std::mutex nodesMutex_;
    mutable std::atomic<bool> nodesReady_{false};
    mutable std::vector<ExecNode> nodes_;
};

/// Computes the plan of `graph` (a frozen graph with nodes). Use GCGraph::execPlan, which keeps
/// the result.
std::unique_ptr<ExecPlan> buildExecPlan(const GCGraph &graph);

/// ---- The anchor, asked from an executor -------------------------------------------------------
//
// One question, two ways of naming the node it is about: a back end that runs nodes holds the node
// (`noteCountedCreationForNode`), and one that runs bytecode holds a pc of the image it executes
// (`noteCountedCreationAt`, which finds the node through the plan's counted-site table). Both reach
// the same decision -- `refProducedAt` at the node's position in the plan -- so the four back ends
// answer with the same instrument and not with four copies of it.
//
// `boxes` is how many boxes the node's execution made, read by the caller from the allocator
// (`core::rtdata::countedBlocksMade`) around the operator call: the caller is the only place that
// can bracket the creation, and the count has to come from the allocator rather than from here.
//
// These report and return nothing. The node has already run, the value is already in its slot and
// the count of the program's values is the plan's business; nothing an anchor learns may refuse a
// creation, reorder a node or take a different path -- an instrument that decides is a test hook.
void noteCountedCreationForNode(const void *graph, gc_node_ref_t node, uint64_t boxes) noexcept;
void noteCountedCreationAt(const void *graph, size_t pc, uint64_t boxes) noexcept;

/// How many boxes the node being run made: the sites bracket the operator call with it, and hand
/// the answer to one of the two calls above.
///
/// It is a bracket and not a single read because the box is made inside the operator, and it is
/// measured on the allocator's per-thread count so that a scheduler running several threads reads
/// its own work. What a nested executor already reported is subtracted: an operator that runs a
/// graph (map, foreach, a loop body) is bracketed by its caller, and the callee's boxes are the
/// callee's nodes' -- reporting them here too would count the same box twice.
class CountedBoxesBracket {
  public:
    /// Read before the operator runs. Called only while --gc-check asks for the counted blocks.
    void begin() noexcept {
        made_     = camel::core::rtdata::countedBlocksMadeOnThisThread();
        reported_ = camel::core::rtdata::countedCreationsReportedOnThisThread();
    }
    /// Read after it: what this node's execution made that no nested site has reported.
    uint64_t sinceBegin() const noexcept {
        const uint64_t made = camel::core::rtdata::countedBlocksMadeOnThisThread() - made_;
        const uint64_t nested =
            camel::core::rtdata::countedCreationsReportedOnThisThread() - reported_;
        return made > nested ? made - nested : 0;
    }

  private:
    uint64_t made_     = 0;
    uint64_t reported_ = 0;
};

/// TEST-ONLY: builds the loop tables of `plan` again from `plan.topo`, exactly as buildExecPlan
/// does once (buildLoopTables), so that a test pass that reorders a plan's `topo` gets the tables
/// that order deserves -- and the same refusal for a loop that is not one range of it. Nothing in a
/// run calls this: a plan is built once, with these tables.
void rebuildLoopTablesForTests(const GCGraph &graph, ExecPlan &plan);

} // namespace camel::runtime