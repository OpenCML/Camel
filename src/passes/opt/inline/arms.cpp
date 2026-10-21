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
 * The arms of the branches of a program, brought into the graph that holds the branch.
 *
 * The front end builds each arm as a graph of its own, called by a FUNC node placed after the
 * BRCH. This splices every such call (inlineCallInOrder), so the arm becomes an Arm region of the
 * graph that has the branch and the graph of the arm is no longer called.
 */
#include "camel/passes/opt/inline.h"

#include "camel/runtime/draft_session.h"

#include <unordered_set>

namespace camel::passes::opt {

using camel::runtime::GCNodeKind;
using camel::runtime::gc_node_ref_t;
using camel::runtime::kInvalidNodeRef;

namespace {

/// The first FUNC node of the draft that is the head of an arm of a BRCH and is not the head an
/// earlier splice left (`spliced`: the body of an arm may start with a call), or kInvalidNodeRef.
gc_node_ref_t findArmCall(
    const camel::runtime::GraphDraft &draft, const std::unordered_set<gc_node_ref_t> &spliced) {
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        const auto *header = draft.header(id);
        if (!header || header->kind != GCNodeKind::Func || spliced.contains(id)) {
            continue;
        }
        for (gc_node_ref_t pred : draft.ctrlInputsOf(id)) {
            const auto *predHeader = draft.header(pred);
            if (!predHeader || predHeader->kind != GCNodeKind::Brch) {
                continue;
            }
            for (const auto &arm : draft.branchArmsOf(pred)) {
                if (arm.head == id) {
                    return id;
                }
            }
        }
    }
    return kInvalidNodeRef;
}

} // namespace

const camel::runtime::Program *lowerBranchArms(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *root, bool detached) {
    camel::runtime::RuntimeGraphDraftSession session(
        context,
        program,
        root != nullptr ? root : program->root());
    bool changed = false;
    for (camel::runtime::GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        bool hasBranch = false;
        for (auto it = graph->nodes().begin(); it != graph->nodes().end() && !hasBranch; ++it) {
            hasBranch = graph->node(it.ref())->kind == GCNodeKind::Brch;
        }
        if (!hasBranch) {
            continue;
        }
        camel::runtime::GraphDraft &draft = session.edit(graph);
        std::unordered_set<gc_node_ref_t> spliced;
        std::vector<camel::runtime::GraphId> armGraphs;
        for (gc_node_ref_t call = findArmCall(draft, spliced); call != kInvalidNodeRef;
             call               = findArmCall(draft, spliced)) {
            armGraphs.push_back(draft.calleeIdOf(call));
            const auto inlined = inlineCallInOrder(session, draft, call);
            if (!inlined) {
                break;
            }
            spliced.insert(inlined.ctrlEntry);
            changed = true;
        }
        draft.dropUncalledGraphs(armGraphs);
    }
    return changed ? session.commit(detached) : program;
}

} // namespace camel::passes::opt
