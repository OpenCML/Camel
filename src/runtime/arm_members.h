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
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The nodes of an arm of a BRCH, found from the BRCH, its arm anchors and the users and inputs
 * of the nodes, over any graph that offers the View below (a frozen graph, a draft). One
 * algorithm for the execution plan, which looks for where an arm starts, and for the regions
 * that record the arms.
 *
 * The arm is what runs only when it is taken: the nodes that follow its head down to the JOIN
 * (a dead node is not followed), its tail when the tail is inside the branch, and what these
 * read, transitively, that is not read to select the arm.
 *
 * View:
 *   bool contains(ref)  bool live(ref)  ref join(brch)  GCBranchArm arm(brch, k)
 *   span ctrlIn/normIn/withIn(ref)  span ctrlOut/normOut/withOut(ref)
 */

#pragma once

#include "camel/runtime/graph.h"

#include <algorithm>
#include <functional>
#include <unordered_set>
#include <vector>

namespace camel::runtime {

template <typename View>
std::unordered_set<gc_node_ref_t> armMembers(const View &view, gc_node_ref_t brch, size_t armIndex) {
    const gc_node_ref_t join = view.join(brch);
    const GCBranchArm arm    = view.arm(brch, armIndex);

    std::unordered_set<gc_node_ref_t> preBranchDeps;
    std::function<void(gc_node_ref_t)> collectPreBranchDeps = [&](gc_node_ref_t nodeRef) {
        if (nodeRef == kInvalidNodeRef || nodeRef == brch || nodeRef == join ||
            !view.contains(nodeRef) || !preBranchDeps.insert(nodeRef).second) {
            return;
        }
        for (auto inputs : {view.ctrlIn(nodeRef), view.normIn(nodeRef), view.withIn(nodeRef)}) {
            for (gc_node_ref_t inputRef : inputs) {
                collectPreBranchDeps(inputRef);
            }
        }
    };
    for (auto inputs : {view.ctrlIn(brch), view.normIn(brch), view.withIn(brch)}) {
        for (gc_node_ref_t inputRef : inputs) {
            collectPreBranchDeps(inputRef);
        }
    }

    std::unordered_set<gc_node_ref_t> members;
    std::vector<gc_node_ref_t> worklist{arm.head};
    while (!worklist.empty()) {
        const auto nodeRef = worklist.back();
        worklist.pop_back();
        // Nodes nothing the exit depends on uses are no part of an arm, and what they read need
        // not be either: followed, a dead user of this arm that also reads another arm would
        // pull that arm's code into this one.
        if (nodeRef == kInvalidNodeRef || nodeRef == brch || nodeRef == join ||
            !view.contains(nodeRef) || !view.live(nodeRef) || !members.insert(nodeRef).second) {
            continue;
        }
        for (auto outputs : {view.ctrlOut(nodeRef), view.normOut(nodeRef), view.withOut(nodeRef)}) {
            for (gc_node_ref_t outputRef : outputs) {
                if (outputRef != join) {
                    worklist.push_back(outputRef);
                }
            }
        }
    }

    // An arm's code is also what its tail depends on inside the branch: a rewrite can leave the
    // head a node nothing in the arm follows any more while the rest of the arm waits on the
    // BRCH directly. A tail outside the branch (a value computed before it) is not the arm's.
    if (const gc_node_ref_t tail = arm.tail; tail != kInvalidNodeRef && tail != brch &&
                                             tail != join && view.contains(tail) &&
                                             !members.contains(tail)) {
        std::unordered_set<gc_node_ref_t> seen;
        std::vector<gc_node_ref_t> work{brch};
        bool inBranch = false;
        while (!work.empty() && !inBranch) {
            const gc_node_ref_t nodeRef = work.back();
            work.pop_back();
            for (auto outputs :
                 {view.ctrlOut(nodeRef), view.normOut(nodeRef), view.withOut(nodeRef)}) {
                for (gc_node_ref_t outputRef : outputs) {
                    if (outputRef == tail) {
                        inBranch = true;
                    } else if (outputRef != join && seen.insert(outputRef).second) {
                        work.push_back(outputRef);
                    }
                }
            }
        }
        if (inBranch) {
            members.insert(tail);
        }
    }

    std::unordered_set<gc_node_ref_t> dependencyVisited;
    std::function<void(gc_node_ref_t)> collectInputs = [&](gc_node_ref_t nodeRef) {
        if (nodeRef == kInvalidNodeRef || nodeRef == brch || nodeRef == join ||
            preBranchDeps.contains(nodeRef) || !view.contains(nodeRef) ||
            !dependencyVisited.insert(nodeRef).second) {
            return;
        }
        members.insert(nodeRef);
        for (auto inputs : {view.ctrlIn(nodeRef), view.normIn(nodeRef), view.withIn(nodeRef)}) {
            for (gc_node_ref_t inputRef : inputs) {
                collectInputs(inputRef);
            }
        }
    };
    const std::vector<gc_node_ref_t> found(members.begin(), members.end());
    for (gc_node_ref_t nodeRef : found) {
        collectInputs(nodeRef);
    }
    return members;
}

} // namespace camel::runtime
