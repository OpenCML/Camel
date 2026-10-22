// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Bounded control reduction over mandatory data/control paths, preserving branch boundaries. */

#include "camel/runtime/control.h"

#include <algorithm>
#include <unordered_set>
#include <vector>

namespace camel::runtime {
namespace {

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
    // A GATE must still have at least one CTRL input; conditional boundaries stay separate.
    const auto *source = draft.header(predecessor);
    if (h && source && source->kind != GCNodeKind::Brch && h->kind != GCNodeKind::Loop &&
        h->kind != GCNodeKind::Func && h->kind != GCNodeKind::Call && !isBoundary(draft, user) &&
        (h->kind != GCNodeKind::Gate || !existing.empty())) {
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

size_t reduceControlDependencies(GraphDraft &draft, size_t maxWork) {
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
        if (keep.empty() && h->kind == GCNodeKind::Gate) {
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
    // Epochs avoid clearing graph-sized scratch arrays for every target. Mandatory value
    // inputs seed the walk directly; CTRL inputs seed only their parents, so a control edge
    // cannot prove itself. JOIN arm values are alternatives, never mandatory predecessors:
    // a walk may reach a completed JOIN/LOOP as its endpoint but cannot walk through it.
    std::vector<size_t> candidate(slots, 0), seen(slots, 0), redundant(slots, 0);
    for (gc_node_ref_t target = 0; target < slots && maxWork; ++target) {
        if (protectedNode[target] || draft.ctrlInputsOf(target).empty()) {
            continue;
        }
        const bool hasValues =
            !draft.normInputsOf(target).empty() || !draft.withInputsOf(target).empty();
        if (!hasValues && draft.ctrlInputsOf(target).size() == 1) {
            continue;
        }
        const size_t epoch = static_cast<size_t>(target) + 1;
        std::vector<gc_node_ref_t> roots;
        for (auto pred : draft.ctrlInputsOf(target)) {
            if (candidate[pred] != epoch) {
                candidate[pred] = epoch;
                roots.push_back(pred);
            }
        }
        size_t localWork = std::min<size_t>(256, maxWork);
        std::vector<gc_node_ref_t> work;
        const auto expand = [&](gc_node_ref_t node) {
            if (protectedNode[node]) {
                return;
            }
            for (auto inputs :
                 {draft.normInputsOf(node), draft.withInputsOf(node), draft.ctrlInputsOf(node)}) {
                for (auto pred : inputs) {
                    if (!localWork) {
                        return;
                    }
                    --localWork;
                    --maxWork;
                    work.push_back(pred);
                }
            }
        };
        for (auto inputs : {draft.normInputsOf(target), draft.withInputsOf(target)}) {
            for (auto pred : inputs) {
                if (!localWork) {
                    break;
                }
                --localWork;
                --maxWork;
                work.push_back(pred);
            }
        }
        for (auto root : roots) {
            expand(root);
        }
        while (!work.empty()) {
            const auto node = work.back();
            work.pop_back();
            if (seen[node] == epoch) {
                continue;
            }
            seen[node]    = epoch;
            const auto *h = draft.header(node);
            const bool completedBoundary =
                h &&
                (h->kind == GCNodeKind::Join || h->kind == GCNodeKind::Loop);
            if (candidate[node] == epoch && (!protectedNode[node] || completedBoundary)) {
                redundant[node] = epoch;
            }
            expand(node);
        }
        std::vector<gc_node_ref_t> keep;
        for (auto root : roots) {
            if (redundant[root] != epoch) {
                keep.push_back(root);
            }
        }
        // GATEs require a CTRL input even if a value path already orders them. With no value
        // seed, an empty frontier instead indicates a malformed cycle; preserve diagnostics.
        if (keep.empty() && (draft.header(target)->kind == GCNodeKind::Gate || !hasValues)) {
            keep.push_back(roots.back());
        }
        if (keep.size() < draft.ctrlInputsOf(target).size()) {
            removed += draft.ctrlInputsOf(target).size() - keep.size();
            draft.setCtrlInputs(target, keep);
        }
    }
    return removed;
}

} // namespace camel::runtime
