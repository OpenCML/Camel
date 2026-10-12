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
 * Author: Zhenjie Wei
 * Created: Oct. 04, 2026
 * Updated: Oct. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "fault.h"

#include "camel/runtime/draft_session.h"

camel::runtime::GCGraph *
DropCallArgumentTestPass::apply(camel::runtime::GCGraph *graph, std::ostream &) {
    using camel::runtime::GCFuncBody;
    using camel::runtime::GCNodeKind;
    camel::runtime::RuntimeGraphDraftSession session(context_, graph);
    for (camel::runtime::GCGraph *owner : session.collectReachableRuntimeGraphs()) {
        camel::runtime::GraphDraft &draft = session.edit(owner);
        for (camel::runtime::gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            const auto *header = draft.header(id);
            if (!header || header->kind != GCNodeKind::Func || draft.normInputsOf(id).empty()) {
                continue;
            }
            // The FUNC now passes one argument fewer than its callee has parameters.
            draft.unlinkInput(
                camel::runtime::DraftEdgeKind::Norm,
                id,
                draft.normInputsOf(id).back());
            return session.commit();
        }
    }
    return graph;
}

camel::runtime::GCGraph *
OvergrowGraphTestPass::apply(camel::runtime::GCGraph *graph, std::ostream &) {
    camel::runtime::RuntimeGraphDraftSession session(context_, graph);
    for (camel::runtime::GCGraph *owner : session.collectReachableRuntimeGraphs()) {
        if (owner->name() != "main") {
            continue;
        }
        camel::runtime::GraphDraft &draft = session.edit(owner);
        camel::core::type::Type *type     = nullptr;
        for (camel::runtime::gc_node_ref_t id = 0; id < draft.nodeSlotCount() && !type; ++id) {
            if (const auto *header = draft.header(id)) {
                type = header->dataType;
            }
        }
        // Each node takes several blocks: 20000 of them need more than the 65535 a graph has.
        for (size_t i = 0; i < 20000; ++i) {
            draft.addCopyNode(type);
        }
        return session.commit();
    }
    return graph;
}

namespace {

/// Runs `breakBranch(draft, brch, join)` on the first branch of `main` and commits.
template <typename Fn>
camel::runtime::GCGraph *breakFirstBranchOfMain(
    const camel::core::context::context_ptr_t &context, camel::runtime::GCGraph *graph,
    Fn breakBranch) {
    using camel::runtime::GCNodeKind;
    camel::runtime::RuntimeGraphDraftSession session(context, graph);
    for (camel::runtime::GCGraph *owner : session.collectReachableRuntimeGraphs()) {
        if (owner->name() != "main") {
            continue;
        }
        camel::runtime::GraphDraft &draft = session.edit(owner);
        for (camel::runtime::gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            const auto *header = draft.header(id);
            if (!header || header->kind != GCNodeKind::Brch) {
                continue;
            }
            const auto join = reinterpret_cast<const camel::runtime::DraftBrchPayload *>(
                                  draft.payloadOf(id).data())
                                  ->join;
            breakBranch(draft, id, join);
            return session.commit();
        }
    }
    return graph;
}

} // namespace

camel::runtime::GCGraph *
LeakBranchArmTestPass::apply(camel::runtime::GCGraph *graph, std::ostream &) {
    return breakFirstBranchOfMain(
        context_,
        graph,
        [](camel::runtime::GraphDraft &draft, camel::runtime::gc_node_ref_t brch,
           camel::runtime::gc_node_ref_t join) {
            // A node that runs because of the BRCH and that no arm's result depends on.
            const auto stray = draft.addSyncNode();
            draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, stray, brch);
            draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, join, stray);
        });
}

camel::runtime::GCGraph *
ShareBranchArmTestPass::apply(camel::runtime::GCGraph *graph, std::ostream &) {
    return breakFirstBranchOfMain(
        context_,
        graph,
        [](camel::runtime::GraphDraft &draft, camel::runtime::gc_node_ref_t,
           camel::runtime::gc_node_ref_t join) {
            // The second arm's result now depends on the first arm's.
            const auto tails = draft.withInputsOf(join);
            if (tails.size() >= 2 && tails[0] != tails[1]) {
                const auto first = tails[0], second = tails[1];
                draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, second, first);
            }
        });
}
