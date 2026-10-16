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
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

/* CTRL-only transitive reduction with bounded work and preserved branch boundaries. */
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
    if (predecessor == user || std::ranges::find(existing, predecessor) != existing.end() ||
        hasControlDependency(draft, predecessor, user, maxWork)) {
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
    size_t edges = 0;
    for (gc_node_ref_t id = 0; id < slots; ++id) {
        const auto *h = draft.header(id);
        if (!h) {
            continue;
        }
        edges += draft.ctrlInputsOf(id).size();
        protectedNode[id] = protectedNode[id] || isBoundary(draft, id);
        if (h->kind == GCNodeKind::Brch) {
            for (const auto &arm : draft.branchArmsOf(id)) {
                if (arm.head < slots) {
                    protectedNode[arm.head] = true;
                }
                if (arm.tail < slots) {
                    protectedNode[arm.tail] = true;
                }
            }
        }
    }
    maxWork = std::min(maxWork, 8 * (slots + edges));
    // Epochs avoid clearing graph-sized scratch arrays for every target. Each walk starts at
    // the *parents* of its CTRL predecessors, so reaching a predecessor proves a distinct path.
    std::vector<size_t> candidate(slots, 0), seen(slots, 0), redundant(slots, 0);
    size_t removed = 0;
    for (gc_node_ref_t target = 0; target < slots && maxWork; ++target) {
        if (protectedNode[target] || draft.ctrlInputsOf(target).size() < 2) {
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
            for (auto pred : draft.ctrlInputsOf(node)) {
                if (!localWork) {
                    break;
                }
                --localWork;
                --maxWork;
                work.push_back(pred);
            }
        };
        for (auto root : roots) {
            expand(root);
        }
        while (!work.empty()) {
            const auto node = work.back();
            work.pop_back();
            if (seen[node] == epoch || protectedNode[node]) {
                continue;
            }
            seen[node] = epoch;
            if (candidate[node] == epoch) {
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
        // A valid DAG always has a maximal predecessor. Retain malformed/cyclic input rather
        // than producing a GATE with no control input; validation owns those diagnostics.
        if (!keep.empty() && keep.size() < draft.ctrlInputsOf(target).size()) {
            removed += draft.ctrlInputsOf(target).size() - keep.size();
            draft.setCtrlInputs(target, keep);
        }
    }
    return removed;
}

} // namespace camel::runtime
