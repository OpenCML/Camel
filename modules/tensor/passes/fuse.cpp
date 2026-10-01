/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You may use this software according to the terms and conditions of the
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
 * Created: Sep. 28, 2026
 * Updated: Sep. 28, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The tensor::fuse rewrite (see fuse.h).
 */

#include "fuse.h"

#include "../ops/registry.h"

#include "camel/runtime/draft_session.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>
#include <vector>

namespace camel::tensor::passes {

using camel::runtime::gc_node_ref_t;
using camel::runtime::GCGraph;
using camel::runtime::GCNodeKind;
using camel::runtime::GCOperBody;
using camel::runtime::GCReturnKind;
using camel::runtime::GraphDraft;
using camel::runtime::kInvalidNodeRef;
using camel::runtime::RuntimeGraphDraftSession;

namespace {

std::string_view operUri(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return {};
    }
    return reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data())->uri();
}

/// True when nothing outside the pattern observes `id`: its value feeds only `next` (as a norm
/// input) and its control users, if any, are `next` as well. Since no other node depends on it,
/// merging it into `next` cannot create a cycle, and its own control inputs can move to the
/// fused node (in `sync` code every call is chained this way).
bool isPrivateIntermediate(const GraphDraft &draft, gc_node_ref_t id, gc_node_ref_t next) {
    const auto normUsers = draft.normUsersOf(id);
    const auto ctrlUsers = draft.ctrlUsersOf(id);
    return normUsers.size() == 1 && normUsers[0] == next && draft.withUsersOf(id).empty() &&
           std::ranges::all_of(ctrlUsers, [&](gc_node_ref_t user) { return user == next; }) &&
           id != draft.outputNode() && id != draft.returnNode() && id != draft.exitNode() &&
           !draft.isBranchArmAnchor(id);
}

struct Match {
    gc_node_ref_t matmul;
    gc_node_ref_t add;
    gc_node_ref_t relu; // kInvalidNodeRef when not fused
    gc_node_ref_t bias;
};

/// The first fusable pattern in the draft's current state. Rewrites change operands of other
/// patterns (a fused add may be the bias of the next one), so matches are found one at a time.
std::optional<Match> findMatch(const GraphDraft &draft) {
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (operUri(draft, id) != "tensor:add") {
            continue;
        }
        const auto ins = draft.normInputsOf(id);
        if (ins.size() != 2) {
            continue;
        }
        // Float addition is commutative, so the product may be either operand.
        for (size_t k = 0; k < 2; ++k) {
            const gc_node_ref_t mm = ins[k];
            if (operUri(draft, mm) != "tensor:matmul" || draft.normInputsOf(mm).size() != 2 ||
                !isPrivateIntermediate(draft, mm, id)) {
                continue;
            }
            Match m{.matmul = mm, .add = id, .relu = kInvalidNodeRef, .bias = ins[1 - k]};
            const auto users = draft.normUsersOf(id);
            if (users.size() == 1 && operUri(draft, users[0]) == "tensor:relu" &&
                isPrivateIntermediate(draft, id, users[0])) {
                m.relu = users[0];
            }
            return m;
        }
    }
    return std::nullopt;
}

void fuse(GraphDraft &draft, const Match &m) {
    const bool withRelu      = m.relu != kInvalidNodeRef;
    const gc_node_ref_t root = withRelu ? m.relu : m.add;
    const std::string uri    = withRelu ? "tensor:matmul_add_relu" : "tensor:matmul_add";
    const auto *def          = ops::OpRegistry::instance().find(uri);
    const auto mmInputs      = draft.normInputsOf(m.matmul);
    const std::array inputs  = {mmInputs[0], mmInputs[1], m.bias};

    // The fused node inherits every control dependency of the pattern that comes from outside it.
    const std::array pattern = {m.matmul, m.add, m.relu};
    std::vector<gc_node_ref_t> ctrl;
    for (gc_node_ref_t node : pattern) {
        if (node == kInvalidNodeRef) {
            continue;
        }
        for (gc_node_ref_t pred : draft.ctrlInputsOf(node)) {
            if (std::ranges::find(pattern, pred) == pattern.end() &&
                std::ranges::find(ctrl, pred) == ctrl.end()) {
                ctrl.push_back(pred);
            }
        }
    }

    const gc_node_ref_t fused = draft.addOperNode(draft.header(root)->dataType, def->kernel, uri);
    draft.setNormInputs(fused, inputs);
    draft.setCtrlInputs(fused, ctrl);

    draft.replaceAllValueUses(root, fused);
    draft.replaceAllCtrlUses(root, fused);
    draft.retargetBranchArmAnchors(root, fused, fused);
    if (draft.outputNode() == root) {
        draft.setOutputNode(fused);
    }
    if (draft.returnNode() == root) {
        draft.setReturnNode(fused, draft.returnKind());
    }
    draft.eraseNode(root);
    if (withRelu) {
        draft.eraseNode(m.add);
    }
    draft.eraseNode(m.matmul);
}

} // namespace

GCGraph *TensorFusePass::apply(GCGraph *graph, std::ostream &) {
    RuntimeGraphDraftSession session(context_, graph);
    size_t fusedCount = 0;
    for (GCGraph *g : session.collectReachableRuntimeGraphs()) {
        GraphDraft &draft = session.edit(g);
        while (const auto m = findMatch(draft)) {
            fuse(draft, *m);
            ++fusedCount;
        }
    }
    CAMEL_LOG_INFO_S("tensor::fuse", "fused {} matmul+add pattern(s)", fusedCount);
    if (fusedCount == 0) {
        return graph;
    }
    session.commit();
    return context_->runtimeRootGraph();
}

void registerTensorPasses() {
    registerModulePass("tensor::fuse", [](const core::context::context_ptr_t &ctx) {
        return std::make_unique<TensorFusePass>(ctx);
    });
}

} // namespace camel::tensor::passes
