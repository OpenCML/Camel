// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Draft-editing helpers shared by the generic optimization passes.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/mm/root_handle.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camel::passes::generic {

using camel::runtime::gc_node_ref_t;

/// Whether `node` is computed after `wanted`: whether what `node` reads -- its value, `with`,
/// control and sync inputs, walked back through the nodes on the way -- reaches `wanted`. An edge
/// that ordered `wanted` after such a node would put the graph on a cycle of inputs, which the seal
/// check refuses (docs/design/graph-store 5.4), so a wait may only be written on a node that does
/// not read what waits.
inline bool isComputedAfter(
    const camel::runtime::GraphTransaction &transaction, gc_node_ref_t node, gc_node_ref_t wanted,
    size_t maxWork = 8192) {
    const auto &draft = transaction.graph();
    if (node == wanted || wanted == camel::runtime::kInvalidNodeRef) {
        return true;
    }
    std::vector<gc_node_ref_t> work;
    std::vector<bool> seen(draft.nodeSlotCount(), false);
    for (auto group :
         {draft.normInputsOf(node),
          draft.withInputsOf(node),
          draft.ctrlInputsOf(node),
          draft.syncInputsOf(node)}) {
        work.insert(work.end(), group.begin(), group.end());
    }
    while (!work.empty() && maxWork > 0) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (n == wanted) {
            return true;
        }
        if (n >= seen.size() || seen[n] || draft.header(n) == nullptr) {
            continue;
        }
        seen[n] = true;
        for (auto group :
             {draft.normInputsOf(n),
              draft.withInputsOf(n),
              draft.ctrlInputsOf(n),
              draft.syncInputsOf(n)}) {
            for (gc_node_ref_t input : group) {
                --maxWork;
                work.push_back(input);
            }
        }
    }
    return false;
}

/// Hand a wait a removed node carried through a `sync` block's chain to a consumer, in the kind it
/// had. Written only where the chain can take it: the predecessor is a statement exit or a chain
/// head, the consumer is a statement entry or the block's value, and the link cannot fork.
/// Returns whether the wait was written; a caller that gets `false` keeps its conservative
/// fallback.
inline bool handOverSyncWait(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t pred, gc_node_ref_t user) {
    auto &draft = transaction.edit();
    if (!draft.alive(pred) || !draft.alive(user) || pred == user) {
        return false;
    }
    const auto *predHeader = draft.header(pred);
    const auto *userHeader = draft.header(user);
    if (predHeader == nullptr || userHeader == nullptr) {
        return false;
    }
    const bool entryCanFollow =
        camel::runtime::isChainStatement(draft, user) ||
        (userHeader->kind != camel::runtime::GCNodeKind::Join && draft.syncUsersOf(user).empty() &&
         camel::runtime::isBlockValue(draft, user));
    const bool exitCanPrecede =
        camel::runtime::isChainExit(draft, pred) ||
        (predHeader->kind != camel::runtime::GCNodeKind::Join && draft.syncInputsOf(pred).empty());
    if (!entryCanFollow || !exitCanPrecede || !draft.syncInputsOf(user).empty() ||
        !draft.syncUsersOf(pred).empty() || draft.nodeRegion(pred) != draft.nodeRegion(user)) {
        return false;
    }
    const gc_node_ref_t only[1] = {pred};
    draft.setSyncInputs(user, only);
    const auto ctrl = draft.ctrlInputsOf(user);
    if (std::ranges::find(ctrl, pred) == ctrl.end()) {
        draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, user, pred);
    }
    return true;
}

/// The integer neutral elements (x + 0, 0 + x, x - 0, x * 1, 1 * x), the implementation of
/// `std::opt::algebra neutral_only=true`. The algebraic laws themselves are the rules library's
/// (`rules::simplify`, registered as `std::opt::algebra`); this residual exists only because the
/// library has no neutral-only mode, and does nothing without `neutralOnly`.
size_t algebraDraft(
    camel::runtime::GraphTransaction &transaction, bool safeFp = false, bool neutralOnly = false);

/// Bypass a non-anchor forwarding gate by putting its waits on mandatory consumers.
/// Conditional value consumers and costly shared barriers stay gates; edgeBudget bounds growth.
bool bypassValueGate(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t id, size_t &edgeBudget);

/// URI of an OPER node, or an empty view for other nodes. For messages: passes recognise an
/// operator by operIdOf.
std::string_view operUriOf(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/// The operator an OPER node calls (camel/core/operator_table.h), or kNoOper for other nodes.
camel::core::OperId operIdOf(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/// True for an OPER node a pass may compute ahead of time or merge with an identical call: its
/// operator is pure and foldable. Whether the node may also be dropped, moved or reordered is a
/// different question, which only `nodeIsFreelySchedulable` (camel/runtime/graph.h) answers.
bool isPureOper(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/**
 * Whether the node's running is something the program observes, apart from its value being read: an
 * operator with an effect or one that may fault (design principles 3.4's predicate, asked with the
 * shapes of the types so that an elementwise operator over fixed shapes is a computation that
 * cannot fail), a conversion that can fail, or a call -- the callee may do either.
 *
 * An arm, a join, a gate, a loop or a function value is structure, and removing the last read of
 * one removes no running of its own: the read a branch hands on is its arms' running, not the
 * branch's. A removal that takes the last use of an observed run hands it to a `GATE` (3.3).
 */
bool isObservedRun(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/**
 * Replaces a pure operator by the constant its argument types fix, through the operator the core
 * registered a type folder for (`OperatorTable::typeFolder`), and returns whether a value was
 * written. `tensor:shape`, `tensor:numel` and `tensor:dim` are the three today
 * (`modules/tensor/ops/layout_ops.cpp#shapeFold`, `gradient_ops.cpp`): their result is a fact of
 * the argument's type, so no observation of a value decides it.
 *
 * The fold is the same one wherever it runs, and it runs in two places for two reasons:
 * `foldDraft` (the `std::opt::fold` stage) reaches it for a node whose arguments are not all
 * constants, and `foldTypedOperators` (generic.h) offers every operator of a program to it on its
 * own, for a pass that reads a graph structurally before it rewrites it. The second caller exists
 * because what the fold decides can be the difference between a graph a consumer accepts and one
 * it refuses -- `tiles::compile` cuts a `#gpu` scope into segments, and a `shape` operator inside
 * one is an operator no device program exists for, while the constant folded here is a value the
 * segment reads.
 *
 * `roots` keeps a value the fold materializes alive until the commit moves it into the graph's
 * static area; it must outlive the transaction's commit, which is why it is the caller's.
 * `constantBudget` bounds the bytes of constant this may write.
 */
bool foldFromTypes(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t id,
    std::deque<camel::core::mm::RootHandle> &roots, size_t constantBudget);

/// True for nodes whose only observable result is their value, so that a pass may arrange them
/// anywhere: static DATA and the COPY/ACCS/FILL/CAST/OPER nodes that are freely schedulable (pure
/// and unable to fault).
bool isValueOnly(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/// True for an unread node that may be removed: a value-only node, or a pure computation that
/// may fault and sits on no chain -- nothing waits for it and it waits for nothing -- which never
/// ran (design principles 3.1: a node runs if and only if this run uses it). A pure node that may
/// fault and is on a `sync` chain stays: its fault is observable (3.4).
bool isDeadWhenUnread(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/// True unless `id` anchors the graph (exit, output, return, or a branch-arm head/tail).
bool isReplaceable(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/// True when `to` is reachable from `from` along value or control edges (to is downstream).
bool reaches(
    const camel::runtime::GraphTransaction &transaction, gc_node_ref_t from, gc_node_ref_t to);

/**
 * The ordered nodes `id`'s value is computed from: walking up its value inputs, and the control
 * inputs of pure nodes on the way, the first nodes that carry ordering (calls, branches,
 * effects, gates that wait). Code reading `id` runs after them because of the data dependency
 * alone, which replacing `id` by a constant, or inlining a call that ignores an argument,
 * removes.
 */
std::vector<gc_node_ref_t>
orderedSources(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/**
 * True when nothing the value of `id` is computed from reads a slot the program writes: walking up
 * its value inputs -- and the waits of the nodes on the way, which is what orders a computation
 * inside a `sync` block -- the walk meets no node written in place. Replacing such a computation by
 * one value gives every reader the value of a single read of that slot.
 */
bool readsNoWrittenSlot(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/**
 * The value that stands for what a rewrite removes: `value`, with the nodes whose last read the
 * removal takes hung on a `GATE` on the way (design principles 3.3).
 *
 * A pass that removes the last use of a node whose running is observed -- an effect, or a
 * computation that may fault -- may not leave it leaning on an order edge: an order edge only
 * orders, it keeps nothing alive (3.3 as amended, 9.7). It hands that use to a `GATE` instead: one
 * data port reading the value that was there, one sync predecessor naming that node, the value
 * passed through unchanged (docs/design/effects/edges.cn.md 5.3). A sync edge carries "used", so
 * the node still runs, while the other readers of the value are neither pushed behind a chain nor
 * pulled alive by one. Written back to source, it is a `sync` block.
 *
 * `removed` is the node whose going takes the reads; `alsoGoing` names nodes whose reads do not
 * count because they go away too (a dissolved branch and its join, the code of an arm that is not
 * taken). Returns `value` itself when no read goes out with the removal.
 *
 * This is reached, and it is not a corner only a test can build: `std::opt` over the three cases in
 * test/cases/opt/effects -- branch_keeps_outer_effect, dce_effect_under_pure_arithmetic,
 * fold_keeps_arm_effect -- draws keepers for each of them. Where
 * a `sync` block gives the effect a place in its chain the other mechanism below takes over instead
 * -- the inherited waits and the chain place of a dissolved branch -- and the cases that guard that
 * mechanism are in test/plans/feat/opt/unused_value_chain.plan.toml (`unused_effect_chain` and
 * `unused_effect_chain_param`: with the inherited waits switched off, twelve of them go red, and
 * the cases one would expect to guard it, in test/plans/feat/opt/effects_kept.plan.toml, stay
 * green).
 *
 * With `value` invalid the removal leaves nothing in the value -- the node is read by no one, which
 * is why it goes -- and the gate reads that node's own value: it is then the statement that reads
 * the node, and the caller hands it the place the removed node stood in (`handChainPlaceTo`). The
 * last node built is returned, or the invalid ref when nothing goes out.
 *
 * When this is reached: `replaceNode` calls it for a node with no sync user -- one that holds no
 * place in a `sync` block's chain. A node that does hold one is not this case: the block hands its
 * place on to the last reader instead (the block below in `replaceNode`, `handChainPlaceTo`), which
 * is what keeps 3.3 true for every graph the builder writes. The shape this answers is measured and
 * not hypothetical: `std::opt` over test/cases/opt/effects/dce_effect_under_pure_arithmetic.cml
 * draws it. Without the gate, the commit refuses the graph -- "effect node 1
 * (':io/println') with neither a read of its value nor a place in its chain".
 */
gc_node_ref_t keepThroughValue(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t removed, gc_node_ref_t value,
    const std::function<bool(gc_node_ref_t)> &alsoGoing = {});

/**
 * Replaces node `id` by `replacement` (or just removes it when `replacement` is invalid, which
 * requires `id` to have no value users) and erases it. Ordering is preserved: every user of
 * `id`, value or control, inherits `id`'s control predecessors, because a pure node on a
 * control chain still orders its dependents after the effects before it, and the ordered nodes
 * `id`'s value was computed from (calls, effects, gates that wait), because reading a value
 * orders code after its producer and a constant replacing it would not. This cannot create a
 * cycle (predecessors run before `id`, which runs before its users). The caller must ensure
 * `replacement` is not downstream of `id` (see reaches()).
 */
void replaceNode(
    camel::runtime::GraphTransaction &transaction, gc_node_ref_t id, gc_node_ref_t replacement);

/**
 * True when `id` stands in a block's chain: one judgement of what a place in a chain is, asked by
 * the fold and by the constant merge, and written once.
 *
 * A statement holds its place from either side: it is written after the one before it
 * (`syncInputsOf`) and it is what the statement after it is written after (`syncUsersOf`). Reading
 * one side only is what went wrong twice, in two different rewrites. A GATE at the head of a chain
 * has no predecessor, so a rule that asked only whether the gate waits merged it into the constant
 * it forwarded; a `:str/format` in the middle of a chain is not a gate, so a rule that asked only
 * about gates replaced it by the constant it computed. Either way a statement stood where a value
 * then was, and the statement after it kept a control wait only -- which orders and does not run
 * (docs/design/effects/edges.cn.md 1.5, 4.6, 5.3, 5.7; design principles 3.1, 5.7).
 * `test/cases/compile/surface/dropped_value_statement.cml` draws the first shape.
 */
bool holdsChainPlace(const camel::runtime::GraphTransaction &transaction, gc_node_ref_t id);

/// Removes chain GATEs left with no value user, no sync successor, and no block result.
size_t eraseOrphanedChainGates(camel::runtime::GraphTransaction &transaction);

/**
 * The graphs the passes of one run found nothing to rewrite in. What a pass of this file does to
 * a graph depends on that graph alone (its nodes, constants and types; not the graphs it calls),
 * so a graph a pass left alone is left alone again by the same pass for as long as that content
 * is the same, whichever graph object holds it. Rounds of the integrated optimizer ask each pass
 * about every graph, and most graphs stop changing after the first round.
 */
class RewriteMemo {
  public:
    bool leftAlone(std::string_view passName, const camel::runtime::GCGraph *graph) const;
    void remember(std::string_view passName, const camel::runtime::GCGraph *graph);

  private:
    std::unordered_map<std::string, std::unordered_set<uint64_t>> contentsByPass_;
};

/**
 * Whether the pass named may rewrite something in a graph, read from the frozen graph. They are
 * necessary conditions, not exact ones: false means the pass would find nothing, true means it
 * might. Opening a draft of a graph costs more than looking at it, and most graphs have nothing
 * for a given pass.
 */
bool mayFold(const camel::runtime::GCGraph &graph);
bool mayMerge(const camel::runtime::GCGraph &graph);
bool mayAlgebra(const camel::runtime::GCGraph &graph);
bool mayRemoveDead(const camel::runtime::GCGraph &graph);
bool mayReduceOrder(const camel::runtime::GCGraph &graph);

/**
 * Runs `rewrite` on the draft of every graph reachable from the root of `program` and commits
 * when anything changed. `rewrite` returns the number of rewrites it made (their total is stored
 * in `rewrites` when given). Returns the program to continue with. A graph for which
 * `mayRewrite` is given and false, or which the `memo` knows the pass to have left alone, is not
 * given to `rewrite`; with `checkScreens` it is, and a rewrite there is an error (a screen that
 * is too strict).
 */
const camel::runtime::Program *rewriteReachableGraphs(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    std::string_view passName,
    const std::function<size_t(camel::runtime::GraphTransaction &)> &rewrite,
    size_t *rewrites = nullptr, RewriteMemo *memo = nullptr,
    bool (*mayRewrite)(const camel::runtime::GCGraph &) = nullptr, bool checkScreens = false);

} // namespace camel::passes::generic
