// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Bounded control reduction over mandatory data/control paths, preserving branch boundaries. */

#include "camel/runtime/control.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace camel::runtime {
namespace {

/// Removing one control edge can make the next one provable -- a target's own list is read again,
/// and a walk stops at the nodes a boundary protects -- so one graph is swept until a sweep adds
/// nothing. The count is a guard, not a budget: a graph settles in a couple of sweeps.
constexpr size_t kMaxControlSweeps = 8;

bool isBoundary(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    return !h || h->kind == GCNodeKind::Brch || h->kind == GCNodeKind::Join;
}

bool controlPath(
    const GraphDraft &draft, gc_node_ref_t predecessor, gc_node_ref_t user, size_t &maxWork) {
    if (predecessor == user || isBoundary(draft, predecessor) || isBoundary(draft, user)) {
        return false;
    }
    std::vector<gc_node_ref_t> work{user};
    std::unordered_set<gc_node_ref_t> seen;
    while (!work.empty() && maxWork) {
        const auto node = work.back();
        work.pop_back();
        if (!seen.insert(node).second || isBoundary(draft, node)) {
            continue;
        }
        for (auto input : draft.ctrlInputsOf(node)) {
            if (!maxWork) {
                return false;
            }
            --maxWork;
            if (input == predecessor) {
                return true;
            }
            work.push_back(input);
        }
    }
    return false;
}

} // namespace

bool hasControlDependency(
    const GraphDraft &draft, gc_node_ref_t predecessor, gc_node_ref_t user, size_t maxWork) {
    return controlPath(draft, predecessor, user, maxWork);
}

std::vector<gc_node_ref_t>
controlFrontier(const GraphDraft &draft, std::span<const gc_node_ref_t> inputs, size_t maxWork) {
    std::vector<gc_node_ref_t> roots;
    std::unordered_set<gc_node_ref_t> unique;
    for (auto input : inputs) {
        if (unique.insert(input).second) {
            roots.push_back(input);
        }
    }
    std::vector<bool> removed(roots.size(), false);
    for (size_t i = 0; i < roots.size() && maxWork; ++i) {
        for (size_t j = 0; j < roots.size() && maxWork; ++j) {
            if (i == j || removed[j]) {
                continue;
            }
            --maxWork; // bound pair probes as well as the edges they inspect
            if (controlPath(draft, roots[i], roots[j], maxWork)) {
                removed[i] = true;
                break;
            }
        }
    }
    std::vector<gc_node_ref_t> keep;
    for (size_t i = 0; i < roots.size(); ++i) {
        if (!removed[i]) {
            keep.push_back(roots[i]);
        }
    }
    return keep;
}

bool appendControlDependency(
    GraphDraft &draft, gc_node_ref_t predecessor, gc_node_ref_t user, size_t maxWork) {
    const auto existing = draft.ctrlInputsOf(user);
    if (predecessor == user || std::ranges::find(existing, predecessor) != existing.end()) {
        return false;
    }
    const auto *h = draft.header(user);
    // A direct value input already orders ordinary users after its producer. Calls are
    // excluded: the inliner intentionally adds explicit argument ordering just before it
    // removes the call and may discard an unused argument. That edge must reach the body.
    // A GATE must still have at least one CTRL input while its wait is only a control edge
    // (`docs/design/effects/edges.cn.md` 5.3): its wait is the sync predecessor, so a gate that has
    // one needs no control input at all, and nothing here appends one for its sake. The owner of these
    // rules is the finished graph, executed by the seal check at commit, not the draft's writers.
    const auto *source = draft.header(predecessor);
    if (h && source && source->kind != GCNodeKind::Brch && h->kind != GCNodeKind::Loop &&
        h->kind != GCNodeKind::Func && h->kind != GCNodeKind::Call && !isBoundary(draft, user) &&
        (h->kind != GCNodeKind::Gate || !existing.empty() || !draft.syncInputsOf(user).empty())) {
        for (auto inputs : {draft.normInputsOf(user), draft.withInputsOf(user)}) {
            for (auto input : inputs) {
                if (!maxWork) {
                    break;
                }
                --maxWork;
                if (input == predecessor) {
                    return false;
                }
            }
        }
    }
    if (hasControlDependency(draft, predecessor, user, maxWork)) {
        return false;
    }
    draft.appendInput(DraftEdgeKind::Ctrl, user, predecessor);
    return true;
}

size_t reduceControlDependencies(camel::runtime::GraphTransaction &transaction, size_t maxWork) {
    auto &draft = transaction.edit();
    if (!maxWork) {
        return 0;
    }
    const size_t slots = draft.nodeSlotCount();
    std::vector<bool> protectedNode(slots, false);
    size_t edges   = 0;
    size_t removed = 0;
    // Nothing runs a port or a constant: waiting for one orders nothing. (The front end orders
    // the code of a branch arm after the values the arm reads, whatever they are.) A GATE
    // keeps its control input, which it needs.
    for (gc_node_ref_t id = 0; id < slots; ++id) {
        const auto *h = draft.header(id);
        if (!h || draft.ctrlInputsOf(id).empty()) {
            continue;
        }
        std::vector<gc_node_ref_t> keep;
        for (gc_node_ref_t pred : draft.ctrlInputsOf(id)) {
            const auto *source = draft.header(pred);
            if (source && (source->kind == GCNodeKind::Port || source->kind == GCNodeKind::Data)) {
                continue;
            }
            keep.push_back(pred);
        }
        if (keep.empty() && h->kind == GCNodeKind::Gate && draft.syncInputsOf(id).empty()) {
            continue;
        }
        if (keep.size() < draft.ctrlInputsOf(id).size()) {
            removed += draft.ctrlInputsOf(id).size() - keep.size();
            draft.setCtrlInputs(id, keep);
        }
    }
    for (gc_node_ref_t id = 0; id < slots; ++id) {
        const auto *h = draft.header(id);
        if (!h) {
            continue;
        }
        edges += draft.ctrlInputsOf(id).size() + draft.normInputsOf(id).size() +
                 draft.withInputsOf(id).size();
        protectedNode[id] =
            protectedNode[id] || isBoundary(draft, id) || h->kind == GCNodeKind::Loop;
    }
    maxWork = std::min(maxWork, 8 * (slots + edges));
    // Epochs avoid clearing graph-sized scratch arrays for every target. Mandatory value inputs
    // seed the walk directly; the other control inputs seed only their parents, so a control edge
    // cannot prove itself. JOIN arm values are alternatives, never mandatory predecessors: a walk
    // may reach a completed JOIN/LOOP as its endpoint but cannot walk through it. The owner of these
    // rules is the finished graph, executed by the seal check at commit, not the draft's writers.
    //
    // The sweep runs twice, for two readings of the graph. The first is the value-path reduction the
    // pass has always made. The second reads the sync edge as an ordering of its own: it is the edge
    // a statement of a `sync` block waits through, and it carries "used" with that order, so the
    // control edge the builder writes beside it says nothing more
    // (docs/design/effects/edges.cn.md 1.5). The kinds that still read the control kind themselves
    // keep theirs -- the GATE, whose wait it is, and the call, whose predecessors the inline splice
    // hands to the body through it -- until those paths are on the sync edge too.
    //
    // Both readings prove a wait redundant through the orderings the graph already holds: the value
    // inputs a target is computed from, and, in the second reading, a `sync` block's chain. An
    // order edge proves nothing here: it orders without carrying "used", and a control edge is also
    // how the runtime keeps its predecessor alive.
    std::vector<size_t> candidate(slots, 0), seen(slots, 0), redundant(slots, 0);
    size_t epoch = 0;
    const auto reduce = [&](bool readSync) {
        for (size_t sweep = 0; sweep < kMaxControlSweeps && maxWork; ++sweep) {
            const size_t removedBefore = removed;
            for (gc_node_ref_t target = 0; target < slots && maxWork; ++target) {
                if (protectedNode[target] || draft.ctrlInputsOf(target).empty()) {
                    continue;
                }
                const auto *targetHeader = draft.header(target);
                if (readSync && targetHeader != nullptr &&
                    (targetHeader->kind == GCNodeKind::Gate ||
                     targetHeader->kind == GCNodeKind::Func ||
                     targetHeader->kind == GCNodeKind::Call)) {
                    continue;
                }
                // A wait is given up only where the node waits for more than one thing: the lone
                // control edge of a statement records what it waits for, and the change that stops
                // writing the control kind beside a sync edge takes that one, not this one.
                if (readSync && draft.ctrlInputsOf(target).size() < 2) {
                    continue;
                }
                const bool hasValues =
                    !draft.normInputsOf(target).empty() || !draft.withInputsOf(target).empty();
                const bool hasSync = readSync && !draft.syncInputsOf(target).empty();
                if (!hasValues && !hasSync && draft.ctrlInputsOf(target).size() == 1) {
                    continue;
                }
                const size_t thisEpoch = ++epoch;
                std::vector<gc_node_ref_t> roots;
                for (auto pred : draft.ctrlInputsOf(target)) {
                    if (candidate[pred] != thisEpoch) {
                        candidate[pred] = thisEpoch;
                        roots.push_back(pred);
                    }
                }
                size_t localWork = std::min<size_t>(256, maxWork);
                std::vector<gc_node_ref_t> work;
                const auto push = [&](auto inputs) {
                    for (auto pred : inputs) {
                        if (!localWork) {
                            return;
                        }
                        --localWork;
                        --maxWork;
                        work.push_back(pred);
                    }
                };
                const auto expand = [&](gc_node_ref_t node) {
                    if (protectedNode[node]) {
                        return;
                    }
                    push(draft.normInputsOf(node));
                    push(draft.withInputsOf(node));
                    push(draft.ctrlInputsOf(node));
                    if (readSync) {
                        push(draft.syncInputsOf(node));
                    }
                };
                push(draft.normInputsOf(target));
                push(draft.withInputsOf(target));
                if (readSync) {
                    push(draft.syncInputsOf(target));
                }
                for (auto root : roots) {
                    expand(root);
                }
                while (!work.empty()) {
                    const auto node = work.back();
                    work.pop_back();
                    if (seen[node] == thisEpoch) {
                        continue;
                    }
                    seen[node]    = thisEpoch;
                    const auto *h = draft.header(node);
                    const bool completedBoundary =
                        h &&
                        (h->kind == GCNodeKind::Join || h->kind == GCNodeKind::Loop);
                    if (candidate[node] == thisEpoch &&
                        (!protectedNode[node] || completedBoundary)) {
                        redundant[node] = thisEpoch;
                    }
                    expand(node);
                }
                std::vector<gc_node_ref_t> keep;
                for (auto root : roots) {
                    if (redundant[root] != thisEpoch) {
                        keep.push_back(root);
                    }
                }
                // A GATE keeps one control input only while that input *is* its wait -- a gate with a sync
                // predecessor waits on that (`docs/design/effects/edges.cn.md` 5.3) and needs none.
                // With no value seed, an empty frontier otherwise indicates a malformed cycle;
                // preserve diagnostics.
                if (keep.empty() &&
                    ((draft.header(target)->kind == GCNodeKind::Gate &&
                      draft.syncInputsOf(target).empty()) ||
                     !hasValues)) {
                    keep.push_back(roots.back());
                }
                if (keep.size() < draft.ctrlInputsOf(target).size()) {
                    removed += draft.ctrlInputsOf(target).size() - keep.size();
                    draft.setCtrlInputs(target, keep);
                }
            }
            if (removed == removedBefore) {
                break;
            }
        }
    };
    reduce(false);
    reduce(true);
    return removed;
}

namespace {

/// Which edges a reduction reads as ordering, and which of them it may drop. An edge A -> C goes
/// away when a path of at least two edges that order already orders the two ends inside the region
/// that holds the edge, which is what "when both run the predecessor runs first" makes redundant.
enum class OrderingReading {
    /// `std::opt::order` on a graph a pass still rewrites: order edges are the candidates, and the
    /// proof walks order edges and sync edges -- the two kinds that order without passing "used"
    /// (docs/design/effects/edges.cn.md 1). Data and control edges stay out of the proof: a pass may
    /// still rewrite them, so an order edge they would cover is the conservative answer while they
    /// are there.
    Pass,
    /// A document, compared by the order a program keeps: order edges and sync links are the
    /// candidates, and the proof walks every kind that orders (data, sync, order, control). A chain
    /// link a data path already reaches says nothing more about the order (docs/technical/
    /// gir-interchange.md).
    Document,
};

size_t reduceOrderingEdges(GraphDraft &draft, size_t maxWork, OrderingReading reading) {
    if (!maxWork) {
        return 0;
    }
    const bool readsEveryKind = reading == OrderingReading::Document;
    const size_t slots        = draft.nodeSlotCount();
    std::vector<size_t> seen(slots, 0);
    size_t removed = 0;
    size_t epoch   = 0;
    for (gc_node_ref_t user = 0; user < slots && maxWork; ++user) {
        // Copied: dropping one list rebuilds the node, which moves the storage the other list
        // is a view of.
        const auto orderInputs = draft.orderInputsOf(user);
        const auto syncInputs  = draft.syncInputsOf(user);
        const std::vector<gc_node_ref_t> order(orderInputs.begin(), orderInputs.end());
        const std::vector<gc_node_ref_t> sync(syncInputs.begin(), syncInputs.end());
        const bool considerSync = readsEveryKind && !sync.empty();
        if (order.empty() && !considerSync) {
            continue;
        }
        // A graph a pass edits reads a node that two order edges meet in: with one such edge the
        // proof would be a longer path of order edges, which the pass leaves to the document
        // reading. A document also reads a single edge, because the path that implies it may be a
        // data path rather than another ordering edge.
        if (!readsEveryKind && order.size() < 2) {
            continue;
        }
        const uint32_t region = draft.nodeRegion(user);
        const auto implied    = [&](gc_node_ref_t predecessor) {
            ++epoch;
            // Breadth first, so the direct edge is the first level: the walk has proven a longer
            // path only when it reaches `user` at depth two or more.
            bool proven  = false;
            size_t depth = 0;
            std::vector<gc_node_ref_t> level{predecessor};
            seen[predecessor] = epoch;
            while (!level.empty() && !proven && maxWork) {
                ++depth;
                std::vector<gc_node_ref_t> next;
                const auto walk = [&](std::span<const gc_node_ref_t> users) {
                    for (gc_node_ref_t to : users) {
                        if (!maxWork || proven) {
                            return;
                        }
                        --maxWork;
                        if (to == user) {
                            // The direct edge is level one and proves nothing. The pass's
                            // reading stops this node's list there, which is where its screen
                            // left the walk; the document's reading has a kind per list to
                            // walk, so it goes on with the list it has.
                            proven = depth >= 2;
                            if (!readsEveryKind || proven) {
                                return;
                            }
                            continue;
                        }
                        if (seen[to] == epoch || !draft.header(to) ||
                            draft.nodeRegion(to) != region) {
                            continue;
                        }
                        seen[to] = epoch;
                        next.push_back(to);
                    }
                };
                for (gc_node_ref_t from : level) {
                    walk(draft.orderUsersOf(from));
                    walk(draft.syncUsersOf(from));
                    if (readsEveryKind) {
                        walk(draft.normUsersOf(from));
                        walk(draft.withUsersOf(from));
                        walk(draft.ctrlUsersOf(from));
                    }
                    if (proven || !maxWork) {
                        break;
                    }
                }
                level = std::move(next);
            }
            return proven;
        };
        std::vector<gc_node_ref_t> orderKeep;
        for (gc_node_ref_t predecessor : order) {
            if (implied(predecessor)) {
                ++removed;
                continue;
            }
            orderKeep.push_back(predecessor);
        }
        std::vector<gc_node_ref_t> syncKeep;
        if (considerSync) {
            for (gc_node_ref_t predecessor : sync) {
                if (implied(predecessor)) {
                    ++removed;
                    continue;
                }
                syncKeep.push_back(predecessor);
            }
        }
        if (orderKeep.size() != order.size()) {
            draft.setOrderInputs(user, orderKeep);
        }
        if (considerSync && syncKeep.size() != sync.size()) {
            draft.setSyncInputs(user, syncKeep);
        }
    }
    return removed;
}

} // namespace

/// Names the transaction, not the draft, for the same reason the rest of this family does. It still
/// reads control waits: the question of whether a pass may stop making them at all (edges 2a) is
/// undecided, and until it is, a reduction that ignored them would be answering a question about a
/// graph the compiler does not build.
size_t reduceOrderDependencies(camel::runtime::GraphTransaction &transaction, size_t maxWork) {
    auto &draft = transaction.edit();
    return reduceOrderingEdges(draft, maxWork, OrderingReading::Pass);
}

size_t reduceDocumentOrdering(camel::runtime::GraphTransaction &transaction, size_t maxWork) {
    auto &draft = transaction.edit();
    return reduceOrderingEdges(draft, maxWork, OrderingReading::Document);
}

} // namespace camel::runtime

