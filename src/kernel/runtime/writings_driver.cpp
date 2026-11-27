// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The drivers: `rewrite`'s rounds, saturation, and the root scope a condition needs.

#include "camel/runtime/operator_instance.h"
#include "camel/runtime/reachable.h"
#include "camel/runtime/writings.h"
#include "writings_internal.h"

#include "camel/core/operator_table.h"
#include "camel/core/tags.h"
#include "camel/core/type/intern.h"
#include "camel/core/type/text.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/draft_writes.h"
#include "camel/runtime/effect_liveness.h"
#include "camel/runtime/graph_txn.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace camel::runtime {

// ---------------------------------------------------------------- engine roots

namespace {

/// Closes a journal segment on every way out of a scope: a segment left open would keep recording
/// bodies for a caller that is not going to ask for them, and the next segment would not know where
/// its own records begin.
class JournalSegment {
  public:
    explicit JournalSegment(GraphDraft &draft) : draft_(draft) {}
    JournalSegment(const JournalSegment &)            = delete;
    JournalSegment &operator=(const JournalSegment &) = delete;
    ~JournalSegment() { draft_.endUndo(); }

  private:
    GraphDraft &draft_;
};

/// Keeps what the engine holds outside the graph relocatable across the safepoints of a rule
/// condition: the draft's static values and the constant slots the compiled patterns copied out
/// of it. The rules' Program keeps the objects alive; this only updates the copies.
class EngineRootScope {
  public:
    EngineRootScope(GraphTransaction &transaction, std::span<const Rule> rules)
        : transaction_(transaction), rules_(rules) {
        camel::core::mm::autoSpace().registerExternalRootTracer(
            this,
            "rewrite.engineRoots",
            [this](const camel::core::mm::GenerationalAllocatorWithGC::RefRelocator &relocate) {
                transaction_.edit().relocateStaticValues(relocate);
                traceRuleConstants(rules_, relocate);
            });
#ifndef NDEBUG
        // The Debug-only safepoint draft-root check counts this scope for this draft only: a
        // session that rewrites several graphs still needs its RootScope for the others.
        debugDraftRootOpened(transaction_);
#endif
    }
    ~EngineRootScope() {
#ifndef NDEBUG
        debugDraftRootClosed(transaction_);
#endif
        camel::core::mm::autoSpace().unregisterExternalRootTracer(this);
    }
    EngineRootScope(const EngineRootScope &)            = delete;
    EngineRootScope &operator=(const EngineRootScope &) = delete;

  private:
    GraphTransaction &transaction_;
    std::span<const Rule> rules_;
};
} // namespace

using namespace detail;

RewriteStats
DraftWritings::rewrite(std::span<const Rule> rules, size_t maxRounds, RewriteLog *log) {
    for (const Rule &rule : rules) {
        if (rule.twoWay) {
            throw std::invalid_argument(
                "Rule '" + rule.name +
                "' holds both ways (<=>): rewriting applies a rule one way, saturation uses both.");
        }
    }
    if (!draft_.writings().empty()) {
        throw std::logic_error("A rewrite needs a draft with one writing per value.");
    }
    GraphDraft::WritingScope scope(draft_);
    EngineRootScope roots(transaction(), rules);
    // A rewrite is one edit ("一次 rewrite 全成或全不成"): a failure partway takes the draft back to
    // what the call found, so a caller that runs the rewrite again over the same draft gets the
    // result the first run that finished gave. The journal is open for the whole call, and every
    // application marks a point inside it, so one application's all-or-nothing is the inner case of
    // the same mechanism.
    const ApplicationMark start = beginApplication();
    greedy_                     = true;
    RewriteStats stats;
    try {
        auto &ix = *index_;
        // The constants there are, so that a constant a rule needs is found, not made again.
        for (gc_node_ref_t node = 0; node < draft_.nodeSlotCount(); ++node) {
            if (draft_.alive(node) && draft_.header(node)->kind == GCNodeKind::Data &&
                takesPart(node)) {
                const auto *h = draft_.header(node);
                NodeKey key;
                key.kind             = static_cast<uint8_t>(GCNodeKind::Data);
                key.perms            = h->permissions;
                key.region           = draft_.nodeRegion(node);
                const auto slotIndex = static_cast<size_t>(-h->dataIndex);
                const slot_t value   = draft_.staticSlots()[slotIndex];
                const Type *keyType  = h->dataType;
                if (const Type *ty = draft_.staticSlotTypes()[slotIndex]) {
                    keyType = ty;
                }
                key.payload = constantPayload(value, keyType);
                key.typeId  = canonicalTypeIdOf(keyType);
                ix.byKey.try_emplace(key, node);
                if (node >= ix.keyOf.size()) {
                    ix.keyOf.resize(draft_.nodeSlotCount());
                }
                ix.keyOf[node] = key;
            }
        }

        size_t varCount                                = 0;
        std::function<void(const Pattern &)> countVars = [&](const Pattern &p) {
            if (p.kind == Pattern::Kind::Var) {
                varCount = std::max<size_t>(varCount, p.var + 1);
            }
            for (const Pattern &arg : p.args) {
                countVars(arg);
            }
        };
        for (const Rule &rule : rules) {
            countVars(rule.lhs);
            countVars(rule.rhs);
        }

        // Nodes the graph keeps by name; they are never erased. The list is the draft's own answer
        // (`GraphDraft::namesNode`), not a copy of it kept here: the names a graph keeps are the same
        // ones extraction starts from, and the same ones the commit checks a region against, so a
        // second spelling is a second policy -- it was one, and it did not know a control region is
        // entered with its ports.
        const auto kept   = [&](gc_node_ref_t node) { return draft_.namesNode(node); };
        const auto unused = [&](gc_node_ref_t node) {
            return draft_.normUsersOf(node).empty() && draft_.withUsersOf(node).empty() &&
                   draft_.ctrlUsersOf(node).empty() && draft_.syncUsersOf(node).empty();
        };
        // Remove unread nodes admitted by the selected guards, then their unread inputs. Named
        // anchors and remaining users independently keep a node. No keeper restores a guard the
        // caller disabled; a node that takes no part in a rewrite is asked again whether it may go, so
        // an external access or a write that outlives the call is not erased on this path either.
        // `step` records physical erasure, which is distinct from whether a retained node executes.
        const auto eraseUnused = [&](gc_node_ref_t first, RewriteStep *step) {
            std::vector<gc_node_ref_t> work{first};
            while (!work.empty()) {
                const gc_node_ref_t node = work.back();
                work.pop_back();
                if (!draft_.alive(node) || kept(node) || !unused(node) ||
                    (shapeOf(node) == 2 && !deletionAllowed(node))) {
                    continue;
                }
                std::vector<gc_node_ref_t> inputs;
                for (auto part :
                     {draft_.normInputsOf(node),
                      draft_.withInputsOf(node),
                      draft_.ctrlInputsOf(node),
                      draft_.syncInputsOf(node)}) {
                    inputs.insert(inputs.end(), part.begin(), part.end());
                }
                if (node < ix.keyOf.size() && ix.keyOf[node].has_value()) {
                    auto found = ix.byKey.find(*ix.keyOf[node]);
                    if (found != ix.byKey.end() && found->second == node) {
                        ix.byKey.erase(found);
                    }
                    ix.keyOf[node].reset();
                }
                if (step != nullptr) {
                    step->erased.push_back(node);
                    step->erasedText.push_back(nodeText(transaction(), node));
                }
                draft_.eraseNode(node);
                ++stats.erased;
                work.insert(work.end(), inputs.begin(), inputs.end());
            }
        };

        for (; stats.rounds < maxRounds;) {
            ++stats.rounds;
            size_t appliedInRound = 0;
            const size_t slots    = draft_.nodeSlotCount();
            for (gc_node_ref_t node = 0; node < slots; ++node) {
                if (!draft_.alive(node) || !takesPart(node)) {
                    continue;
                }
                ++stats.visited;
                const camel::core::sem::PermissionMask context = contextOf(node);
                matchPerms_                                    = draft_.header(node)->permissions;
                for (const Rule &rule : rules) {
                    if ((rule.needs & ~context) != 0 || rule.lhs.kind != Pattern::Kind::Op) {
                        continue;
                    }
                    // The first match of the rule whose condition holds.
                    std::vector<Item> work{{&rule.lhs, node}};
                    std::vector<gc_node_ref_t> bind(varCount, kInvalidNodeRef);
                    std::vector<uint8_t> bound(varCount, 0);
                    std::vector<gc_node_ref_t> matchedWaits(
                        draft_.ctrlInputsOf(node).begin(),
                        draft_.ctrlInputsOf(node).end());
                    // The operator terms the left side read below its root. The replacement takes
                    // the left side's place, and the terms it no longer reads have to leave the
                    // block's chain with it (below, before the place is handed over).
                    std::vector<gc_node_ref_t> matchedTerms;
                    // A statement of a `sync` block is ordered by the statement written before it, and
                    // that edge is a sync edge (docs/design/effects/edges.cn.md 1.5); a control input
                    // is the other kind of wait such a statement carries, and one taken from the node
                    // already sits in `matchedWaits`. Both are "everything the match read that ordered
                    // it" (below): a law over data inputs
                    // is sound whatever the node is ordered after, and the writing that replaces it
                    // must not be moved across an effect, whichever kind of wait named that effect.
                    for (gc_node_ref_t wait : draft_.syncInputsOf(node)) {
                        if (std::find(matchedWaits.begin(), matchedWaits.end(), wait) ==
                            matchedWaits.end()) {
                            matchedWaits.push_back(wait);
                        }
                    }
                    std::optional<std::vector<gc_node_ref_t>> chosen;
                    std::optional<std::vector<gc_node_ref_t>> chosenWaits;
                    std::optional<std::vector<gc_node_ref_t>> chosenTerms;
                    MatchRemainder remainder;
                    std::optional<MatchRemainder> chosenRemainder;
                    matchFrom(
                        context,
                        work,
                        0,
                        bind,
                        bound,
                        &matchedWaits,
                        &matchedTerms,
                        !ordered_,
                        [&] {
                            if (!chosen &&
                                (!rule.when || rule.when(Match{*this, node, bind, remainder}))) {
                                chosen          = bind;
                                chosenWaits     = matchedWaits;
                                chosenTerms     = matchedTerms;
                                chosenRemainder = remainder;
                                return true;
                            }
                            return false;
                        },
                        &remainder);
                    if (!chosen) {
                        continue;
                    }
                    // The caller selects which local deletion guards veto discarded bindings.
                    // Matching, numeric permissions and structural repair remain independent.
                    if (dropsProtected(rule, *chosen, chosenRemainder.value_or(MatchRemainder{}))) {
                        continue;
                    }
                    const auto *h = draft_.header(node);
                    DraftNodeInit base;
                    base.dataType              = h->dataType;
                    base.permissions           = h->permissions;
                    base.region                = draft_.nodeRegion(node);
                    base.origin                = draft_.nodeOrigin(node);
                    base.attrs                 = draft_.nodeAttrs(node);
                    const size_t before        = draft_.nodeSlotCount();
                    const ApplicationMark mark = markApplication();
                    gc_node_ref_t made         = kInvalidNodeRef;
                    try {
                        made = build(
                            rule.rhs,
                            Match{*this, node, *chosen, chosenRemainder.value_or(MatchRemainder{})},
                            base);
                    } catch (const RuleSkipped &) {
                        // The right side was abandoned partway through building it (a constant of the
                        // rule faults): one application is all or nothing, so what it had already made
                        // goes back with it and the next rule sees the draft it started from.
                        abandonApplication(mark);
                        continue;
                    }
                    if (made == node) {
                        abandonApplication(mark);
                        continue;
                    }
                    // Everything the match read that ordered it: the root's own control inputs and
                    // those of the operator terms the law stops reading. A law over data inputs is
                    // sound whatever the node is ordered after, but the engine must not move the
                    // computation across an effect, so the writing that replaces it waits for all of
                    // them (design principles 3.3, 3.4).
                    const std::vector<gc_node_ref_t> waits =
                        chosenWaits.value_or(std::vector<gc_node_ref_t>{});
                    // A node that already existed cannot be made to wait without risking a cycle.
                    if (!waits.empty() && made < before) {
                        abandonApplication(mark);
                        continue;
                    }
                    // The attribution of this rewrite (rewrite design 8.7). The step is made only
                    // when a log was asked for, so a plain rewrite allocates nothing for it; the
                    // text of the nodes is read now, because the erased ones lose it below.
                    std::optional<RewriteStep> step;
                    RewriteStep *recorded = nullptr;
                    if (log != nullptr) {
                        step.emplace();
                        step->rule   = rule.name;
                        step->ruleId = rule.id;
                        step->round  = stats.rounds;
                        step->matched.push_back(node);
                        step->matchedText.push_back(nodeText(transaction(), node));
                        for (gc_node_ref_t bound : *chosen) {
                            if (bound == kInvalidNodeRef ||
                                std::find(step->matched.begin(), step->matched.end(), bound) !=
                                    step->matched.end()) {
                                continue;
                            }
                            step->matched.push_back(bound);
                            step->matchedText.push_back(nodeText(transaction(), bound));
                        }
                        step->produced     = made;
                        step->producedText = nodeText(transaction(), made);
                        recorded           = &*step;
                    }
                    draft_.takeStatementPlace(node, made);
                    draft_.replaceAllValueUses(node, made);
                    draft_.replaceAllCtrlUses(node, made);
                    for (gc_node_ref_t wait : waits) {
                        if (wait != made) {
                            draft_.appendControlEdge(made, wait);
                        }
                    }
                    if (draft_.entryNode() == node)
                        draft_.setEntryNode(made);
                    if (draft_.exitNode() == node)
                        draft_.setExitNode(made);
                    if (draft_.outputNode() == node)
                        draft_.setOutputNode(made);
                    if (draft_.returnNode() == node)
                        draft_.setReturnNode(made, draft_.returnKind());
                    for (uint32_t r = 0; r < draft_.regionCount(); ++r) {
                        if (draft_.region(r).result == node) {
                            draft_.setRegionResult(r, made);
                        }
                    }
                    ++appliedInRound;
                    ++stats.applied;
                    // The replacement stands for the whole matched left side, not for its root
                    // alone. A term the right side no longer reads was a statement of that left
                    // side -- in a `sync` block the computations the matched expression is written
                    // in run as statements of the block's chain -- and the place it held there is
                    // the left side's to give up. The chain closes over the term (`closeChainOver`:
                    // what the term waited for is what the statements after it wait for, in both
                    // the sync and the control kind), so what the block says where the matched
                    // expression stood is the left side's own statements
                    // (docs/design/effects/edges.cn.md 4).
                    //
                    // A term whose value something outside the left side still reads keeps its
                    // place: closing the chain over a statement another reader waits for would take
                    // an ordering a live statement stands in, so the check asks for terms read by
                    // nothing but the left side itself. A term the right side still reads stands
                    // outside that set by construction -- the code just built for it reads the term
                    // -- so a law that keeps a term as its value keeps the term where it was.
                    //
                    // Closing the chain is all this does. The term is then a value nobody reads on
                    // a chain nobody waits for, and `eraseUnused` right after is what erases it, as
                    // it erases the root's other discarded inputs. A matched term takes part in a
                    // rule
                    // (`takesPart`; the matcher refuses one that does not), so the deletion is the
                    // plain unread-node one and no guard is asked about it: what a rewrite erases
                    // is the pass's to decide (design principles :684).
                    {
                        const std::vector<gc_node_ref_t> leftSide =
                            chosenTerms.value_or(std::vector<gc_node_ref_t>{});
                        const auto sitsInLeftSide = [&](gc_node_ref_t at) {
                            return at == node || std::find(leftSide.begin(), leftSide.end(), at) !=
                                                     leftSide.end();
                        };
                        for (gc_node_ref_t inside : leftSide) {
                            if (inside == node || !draft_.alive(inside)) {
                                continue;
                            }
                            bool readOutside = false;
                            for (gc_node_ref_t user : draft_.normUsersOf(inside)) {
                                readOutside = readOutside || !sitsInLeftSide(user);
                            }
                            for (gc_node_ref_t user : draft_.withUsersOf(inside)) {
                                readOutside = readOutside || !sitsInLeftSide(user);
                            }
                            if (readOutside) {
                                continue;
                            }
                            const std::vector<gc_node_ref_t> ctrlUsers(
                                draft_.ctrlUsersOf(inside).begin(),
                                draft_.ctrlUsersOf(inside).end());
                            draft_.closeChainOver(inside);
                            // The control kind names the same pair beside the sync edge until the
                            // edges go (src/kernel/runtime/graph_edges.cpp, `checkSyncEdges` reads
                            // the sync edge back), so the pairs that named this term are taken away
                            // here -- the replacement's wait on it included, which the closing
                            // above has already turned into a wait on what the term waited for.
                            for (gc_node_ref_t user : ctrlUsers) {
                                if (user == inside || !draft_.alive(user)) {
                                    continue;
                                }
                                std::vector<gc_node_ref_t> kept;
                                for (gc_node_ref_t in : draft_.ctrlInputsOf(user)) {
                                    if (in != inside) {
                                        kept.push_back(in);
                                    }
                                }
                                draft_.setCtrlInputs(user, kept);
                            }
                        }
                    }
                    eraseUnused(node, recorded);
                    if (log != nullptr) {
                        log->push_back(std::move(*step));
                    }
                    break;
                }
            }
            if (appliedInRound == 0) {
                stats.fixpoint = true;
                break;
            }
        }
    } catch (...) {
        greedy_ = false;
        // What the failed run made, changed and erased goes back with it: the inner applications
        // that landed are taken back by the same journal, and the draft a caller holds after a
        // failure is the draft it passed in.
        abandonApplication(start);
        draft_.endUndo();
        throw;
    }
    greedy_ = false;
    draft_.endUndo();
    return stats;
}

SaturateStats DraftWritings::saturate(std::span<const Rule> rules, const Budget &budget) {
    SaturateStats stats;
    GraphDraft::WritingScope scope(draft_);
    EngineRootScope roots(transaction(), rules);
    rebuild();
    struct Found {
        size_t rule;
        gc_node_ref_t root;
        std::vector<gc_node_ref_t> vars;
        MatchRemainder remainder;
    };
    size_t varCount                                = 0;
    std::function<void(const Pattern &)> countVars = [&](const Pattern &p) {
        if (p.kind == Pattern::Kind::Var) {
            varCount = std::max<size_t>(varCount, p.var + 1);
        }
        for (const Pattern &arg : p.args) {
            countVars(arg);
        }
    };
    for (const Rule &rule : rules) {
        countVars(rule.lhs);
        countVars(rule.rhs);
    }
    for (; stats.rounds < budget.rounds; ++stats.rounds) {
        // All the matches of the round are found before any is applied.
        std::vector<Found> found;
        const auto findBegan = std::chrono::steady_clock::now();
        const size_t slots   = draft_.nodeSlotCount();
        // The nodes a rule may be rooted at, by operator, ascending.
        std::unordered_map<camel::core::OperId, std::vector<gc_node_ref_t>> byOper;
        for (gc_node_ref_t node = 0; node < slots; ++node) {
            if (draft_.alive(node) && draft_.header(node)->kind == GCNodeKind::Oper &&
                takesPart(node)) {
                byOper[reinterpret_cast<const GCOperBody *>(draft_.payloadOf(node).data())->oper]
                    .push_back(node);
            }
        }
        for (size_t r = 0; r < rules.size(); ++r) {
            const Rule &rule = rules[r];
            if (rule.lhs.kind != Pattern::Kind::Op) {
                continue;
            }
            const auto candidates = byOper.find(rule.lhs.oper);
            if (candidates == byOper.end()) {
                continue;
            }
            for (gc_node_ref_t node : candidates->second) {
                const camel::core::sem::PermissionMask context = contextOf(node);
                if ((rule.needs & ~context) != 0) {
                    continue;
                }
                matchPerms_ = draft_.header(node)->permissions;
                std::vector<Item> work{{&rule.lhs, node}};
                std::vector<gc_node_ref_t> bind(varCount, kInvalidNodeRef);
                std::vector<uint8_t> bound(varCount, 0);
                MatchRemainder remainder;
                matchFrom(
                    context,
                    work,
                    0,
                    bind,
                    bound,
                    nullptr,
                    nullptr, // saturation leaves the alternatives standing: nothing is erased
                    !ordered_ && !rule.twoWay,
                    [&] {
                        if (!rule.when || rule.when(Match{*this, node, bind, remainder})) {
                            // Saturation uses the same selected admission guards as greedy
                            // rewriting. Extraction chooses from admitted alternatives; it cannot
                            // restore a guard.
                            if (dropsProtected(rule, bind, remainder)) {
                                return false;
                            }
                            found.push_back(Found{r, node, bind, remainder});
                            return true;
                        }
                        return false;
                    },
                    &remainder);
            }
        }
        stats.matches += found.size();
        const auto applyBegan = std::chrono::steady_clock::now();
        stats.findSeconds += std::chrono::duration<double>(applyBegan - findBegan).count();
        // 一回合 = 一段: what a round applies lands together or not at all. The mark is taken before
        // the first application, and the segment is closed when the round lands (`endUndo`) or takes
        // itself back (`abandonRound`). `found` is the round's only temporary area, so a round the
        // budget cuts has nothing on disk to leave half done.
        const ApplicationMark roundMark = beginApplication();
        const JournalSegment closing(draft_);
        const size_t pendingBase = pending_.size();
        size_t roundWritings     = 0;
        size_t roundUnions       = 0;
        size_t appliedThisRound  = 0;
        bool progress            = false;
        bool stopped             = false;
        for (const Found &match : found) {
            const Rule &rule = rules[match.rule];
            // The budget is read against what the round has added on top of what the earlier rounds
            // added: a cut is a stop, not an error, and it belongs to the whole run.
            if (stats.newWritings + roundWritings >= budget.newWritings ||
                stats.unions + roundUnions >= budget.unions) {
                stopped = true;
                break;
            }
            const auto *h = draft_.header(match.root);
            DraftNodeInit base;
            base.dataType              = h->dataType;
            base.permissions           = h->permissions;
            base.region                = draft_.nodeRegion(match.root);
            base.origin                = draft_.nodeOrigin(match.root);
            base.attrs                 = draft_.nodeAttrs(match.root);
            const size_t before        = draft_.nodeSlotCount();
            const ApplicationMark mark = markApplication();
            gc_node_ref_t made         = kInvalidNodeRef;
            try {
                made = build(rule.rhs, Match{*this, match.root, match.vars, match.remainder}, base);
            } catch (const RuleSkipped &) {
                // One application is all or nothing: the writings it made before the fault go back,
                // so the round counts what this saturation added and not what it started and
                // abandoned (16.1, 16.4: `作废就把临时区的指针退回去`).
                abandonApplication(mark);
                continue;
            }
            // The writings of one value share a region: extraction may put any writing of a class
            // in the place of any other, and a node of an arm cannot stand for a value read outside
            // the arm. A right side that is a node of another region -- a bound variable from an
            // enclosing region, as `x + 1 => x` with `x` outside the arm of `&&` -- is therefore
            // no writing of this value, and the equivalence is not recorded. The one-way rewrite
            // still makes the replacement: reading an enclosing value inside an arm is a plain
            // edge.
            if (made != match.root && draft_.nodeRegion(made) != draft_.nodeRegion(match.root)) {
                abandonApplication(mark);
                continue;
            }
            roundWritings += draft_.nodeSlotCount() - before;
            if (draft_.nodeSlotCount() != before) {
                progress = true;
            }
            if (made != match.root && !store_.same(contextOf(match.root), made, match.root)) {
                if (assertEqualDeferred(match.root, made, rule.needs, rule.id)) {
                    ++roundUnions;
                    progress = true;
                }
            }
            ++appliedThisRound;
        }
        if (stopped) {
            // The round did not finish, so none of it lands. What it applied before the cut is
            // reported and taken back: a state with half of a round in it is a state neither the
            // budget nor the fixpoint explains, and the caller could not tell where the budget ran
            // out from it, because the half that stayed is all it would see.
            abandonRound(roundMark, pendingBase);
            stats.appliedThisRound = appliedThisRound;
            stats.stoppedMidRound  = true;
            // The cut round was started, so it is one of the rounds the run reports, and the time it
            // spent is the run's time.
            ++stats.rounds;
            stats.applySeconds +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - applyBegan).count();
            return stats;
        }
        stats.newWritings      += roundWritings;
        stats.unions           += roundUnions;
        stats.appliedThisRound  = appliedThisRound;
        rebuild();
        stats.applySeconds +=
            std::chrono::duration<double>(std::chrono::steady_clock::now() - applyBegan).count();
        if (!progress) {
            stats.saturated = true;
            ++stats.rounds;
            break;
        }
    }
    return stats;
}

} // namespace camel::runtime
