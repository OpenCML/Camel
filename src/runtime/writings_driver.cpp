// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// The drivers: `rewrite`'s rounds, saturation, and the root scope a condition needs.

#include "camel/runtime/writings.h"
#include "writings_internal.h"
#include "camel/runtime/operator_instance.h"
#include "camel/runtime/reachable.h"

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
    greedy_ = true;
    RewriteStats stats;
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

    // Nodes the graph keeps by name; they are never erased.
    const auto kept = [&](gc_node_ref_t node) {
        if (node == draft_.entryNode() || node == draft_.exitNode() ||
            node == draft_.outputNode() || node == draft_.returnNode()) {
            return true;
        }
        for (auto list : {draft_.normPorts(), draft_.withPorts(), draft_.closureNodes()}) {
            if (std::find(list.begin(), list.end(), node) != list.end()) {
                return true;
            }
        }
        for (uint32_t r = 0; r < draft_.regionCount(); ++r) {
            if (draft_.region(r).result == node) {
                return true;
            }
        }
        return false;
    };
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
                MatchRemainder remainder;
                std::optional<MatchRemainder> chosenRemainder;
                matchFrom(
                    context,
                    work,
                    0,
                    bind,
                    bound,
                    &matchedWaits,
                    !ordered_,
                    [&] {
                        if (!chosen &&
                            (!rule.when || rule.when(Match{*this, node, bind, remainder}))) {
                            chosen          = bind;
                            chosenWaits     = matchedWaits;
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
                base.dataType       = h->dataType;
                base.permissions    = h->permissions;
                base.region         = draft_.nodeRegion(node);
                base.origin         = draft_.nodeOrigin(node);
                base.attrs          = draft_.nodeAttrs(node);
                const size_t before = draft_.nodeSlotCount();
                gc_node_ref_t made  = kInvalidNodeRef;
                try {
                    made = build(
                        rule.rhs,
                        Match{*this, node, *chosen, chosenRemainder.value_or(MatchRemainder{})},
                        base);
                } catch (const RuleSkipped &) {
                    continue;
                }
                if (made == node) {
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
    greedy_ = false;
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
        bool progress = false;
        for (const Found &match : found) {
            const Rule &rule = rules[match.rule];
            if (stats.newWritings >= budget.newWritings || stats.unions >= budget.unions) {
                return stats;
            }
            const auto *h = draft_.header(match.root);
            DraftNodeInit base;
            base.dataType       = h->dataType;
            base.permissions    = h->permissions;
            base.region         = draft_.nodeRegion(match.root);
            base.origin         = draft_.nodeOrigin(match.root);
            base.attrs          = draft_.nodeAttrs(match.root);
            const size_t before = draft_.nodeSlotCount();
            gc_node_ref_t made  = kInvalidNodeRef;
            try {
                made = build(rule.rhs, Match{*this, match.root, match.vars, match.remainder}, base);
            } catch (const RuleSkipped &) {
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
                continue;
            }
            stats.newWritings += draft_.nodeSlotCount() - before;
            if (draft_.nodeSlotCount() != before) {
                progress = true;
            }
            if (made != match.root && !store_.same(contextOf(match.root), made, match.root)) {
                if (assertEqualDeferred(match.root, made, rule.needs, rule.id)) {
                    ++stats.unions;
                    progress = true;
                }
            }
        }
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
