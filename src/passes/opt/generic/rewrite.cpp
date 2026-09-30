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
 *
 * Author: Camel Contributors
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Draft-editing helpers shared by the generic optimization passes (see rewrite.h).
 */

#include "rewrite.h"

#include "camel/core/operator_traits.h"
#include "camel/runtime/draft_session.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <vector>

namespace camel::passes::generic {

using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::kInvalidNodeRef;

std::string_view operUriOf(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return {};
    }
    return reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data())->uri();
}

bool isPureOper(const GraphDraft &draft, gc_node_ref_t id) {
    const std::string_view uri = operUriOf(draft, id);
    return !uri.empty() && camel::core::OperatorTraitsRegistry::instance().isPure(uri);
}

bool isValueOnly(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h) {
        return false;
    }
    switch (h->kind) {
    case GCNodeKind::Data:
        return h->dataIndex < 0;
    case GCNodeKind::Cast:
    case GCNodeKind::Copy:
    case GCNodeKind::Accs:
    case GCNodeKind::Fill:
        return true;
    case GCNodeKind::Oper:
        return isPureOper(draft, id);
    default:
        return false;
    }
}

bool isReplaceable(const GraphDraft &draft, gc_node_ref_t id) {
    // The entry node is only a marker (erasing it resets it, and encoding then derives it from
    // the first node), so it does not pin a node; inlining often leaves it on a dead value.
    return id != draft.exitNode() && id != draft.outputNode() && id != draft.returnNode() &&
           !draft.isBranchArmAnchor(id);
}

bool reaches(const GraphDraft &draft, gc_node_ref_t from, gc_node_ref_t to) {
    std::vector<bool> seen(draft.nodeSlotCount(), false);
    std::vector<gc_node_ref_t> work{from};
    while (!work.empty()) {
        const gc_node_ref_t n = work.back();
        work.pop_back();
        if (n == to) {
            return true;
        }
        if (seen[n]) {
            continue;
        }
        seen[n] = true;
        for (auto users : {draft.normUsersOf(n), draft.withUsersOf(n), draft.ctrlUsersOf(n)}) {
            work.insert(work.end(), users.begin(), users.end());
        }
    }
    return false;
}

void replaceNode(GraphDraft &draft, gc_node_ref_t id, gc_node_ref_t replacement) {
    const auto copy = [](std::span<const gc_node_ref_t> refs) {
        return std::vector<gc_node_ref_t>(refs.begin(), refs.end());
    };
    const auto preds = copy(draft.ctrlInputsOf(id));
    std::vector<gc_node_ref_t> users;
    for (auto group : {draft.normUsersOf(id), draft.withUsersOf(id), draft.ctrlUsersOf(id)}) {
        for (gc_node_ref_t u : group) {
            if (std::ranges::find(users, u) == users.end()) {
                users.push_back(u);
            }
        }
    }
    const auto ctrlUsers = copy(draft.ctrlUsersOf(id));

    if (replacement != kInvalidNodeRef) {
        draft.replaceAllValueUses(id, replacement);
    }
    for (gc_node_ref_t user : ctrlUsers) {
        draft.unlinkInput(camel::runtime::DraftEdgeKind::Ctrl, user, id);
    }
    for (gc_node_ref_t user : users) {
        for (gc_node_ref_t pred : preds) {
            const auto existing = draft.ctrlInputsOf(user);
            if (pred != user && std::ranges::find(existing, pred) == existing.end()) {
                draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, user, pred);
            }
        }
    }
    // A control user that waited only on `id` now waits on the value replacing it, so it keeps a
    // control input (a GATE requires one) and still runs after that value exists.
    if (replacement != kInvalidNodeRef) {
        for (gc_node_ref_t user : ctrlUsers) {
            if (user != replacement && draft.ctrlInputsOf(user).empty()) {
                draft.appendInput(camel::runtime::DraftEdgeKind::Ctrl, user, replacement);
            }
        }
    }
    draft.eraseNode(id);
}

GCGraph *rewriteReachableGraphs(
    const camel::core::context::context_ptr_t &context, GCGraph *graph, std::string_view passName,
    const std::function<size_t(GraphDraft &)> &rewrite, size_t *rewrites) {
    camel::runtime::RuntimeGraphDraftSession session(context, graph);
    size_t total = 0;
    for (GCGraph *g : session.collectReachableRuntimeGraphs()) {
        total += rewrite(session.edit(g));
    }
    CAMEL_LOG_INFO_S("Opt", "{}: {} rewrite(s)", passName, total);
    if (rewrites) {
        *rewrites = total;
    }
    if (total == 0) {
        return graph;
    }
    session.commit();
    return context->runtimeRootGraph();
}

} // namespace camel::passes::generic
