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
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The tensor::fuse rewrite (see fuse.h).
 */

#include "fuse.h"

#include "index_eval.h"

#include "../ops/registry.h"

#include "camel/core/operator_semantics.h"
#include "camel/runtime/draft_session.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <cstdlib>
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

/// The operator of an OPER node, kNoOper for any other node.
camel::core::OperId operOf(const GraphDraft &draft, gc_node_ref_t id) {
    const auto *h = draft.header(id);
    if (!h || h->kind != GCNodeKind::Oper) {
        return camel::core::kNoOper;
    }
    return reinterpret_cast<const GCOperBody *>(draft.payloadOf(id).data())->oper;
}

/// The operators the pattern is made of, interned once.
struct PatternOperators {
    camel::core::OperId add    = camel::core::OperatorTable::instance().intern("tensor:add");
    camel::core::OperId matmul = camel::core::OperatorTable::instance().intern("tensor:matmul");
    camel::core::OperId relu   = camel::core::OperatorTable::instance().intern("tensor:relu");
};

const PatternOperators &patternOperators() {
    static const PatternOperators operators;
    return operators;
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
    const PatternOperators &ops = patternOperators();
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (operOf(draft, id) != ops.add) {
            continue;
        }
        const auto ins = draft.normInputsOf(id);
        if (ins.size() != 2) {
            continue;
        }
        // Float addition is commutative, so the product may be either operand.
        for (size_t k = 0; k < 2; ++k) {
            const gc_node_ref_t mm = ins[k];
            // The fused node stands for the whole pattern, so every node of it must have been
            // granted the same numeric permissions (design principles 2.13), which it keeps.
            if (operOf(draft, mm) != ops.matmul || draft.normInputsOf(mm).size() != 2 ||
                !isPrivateIntermediate(draft, mm, id) ||
                draft.nodePermissions(mm) != draft.nodePermissions(id)) {
                continue;
            }
            Match m{.matmul = mm, .add = id, .relu = kInvalidNodeRef, .bias = ins[1 - k]};
            const auto users = draft.normUsersOf(id);
            if (users.size() == 1 && operOf(draft, users[0]) == ops.relu &&
                isPrivateIntermediate(draft, id, users[0]) &&
                draft.nodePermissions(users[0]) == draft.nodePermissions(id)) {
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
    draft.setNodePermissions(fused, draft.nodePermissions(m.add));

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

/// The permissions a library GEMM needs: its own order and grouping of the accumulation
/// (reassociate), and products that may be rounded separately or a different accumulation
/// precision (change_precision). Design principles 2.13.
constexpr uint8_t kLibraryGemmPermissions =
    camel::core::sem::kReassociate | camel::core::sem::kChangePrecision;

/// Replaces every tensor:matmul node granted kLibraryGemmPermissions by tensor:matmul_blas
/// (same operands, control inputs and permissions). Returns the number replaced.
size_t useLibraryGemm(GraphDraft &draft) {
    const PatternOperators &ops = patternOperators();
    const auto *def             = ops::OpRegistry::instance().find("tensor:matmul_blas");
    size_t count                = 0;
    const gc_node_ref_t slots   = draft.nodeSlotCount();
    for (gc_node_ref_t id = 0; id < slots; ++id) {
        if (operOf(draft, id) != ops.matmul ||
            (draft.nodePermissions(id) & kLibraryGemmPermissions) != kLibraryGemmPermissions) {
            continue;
        }
        const auto inputs = draft.normInputsOf(id);
        const auto ctrl   = draft.ctrlInputsOf(id);
        const std::vector<gc_node_ref_t> in(inputs.begin(), inputs.end());
        const std::vector<gc_node_ref_t> ctl(ctrl.begin(), ctrl.end());
        const gc_node_ref_t blas =
            draft.addOperNode(draft.header(id)->dataType, def->kernel, "tensor:matmul_blas");
        draft.setNormInputs(blas, in);
        draft.setCtrlInputs(blas, ctl);
        draft.setNodePermissions(blas, draft.nodePermissions(id));
        draft.replaceAllValueUses(id, blas);
        draft.replaceAllCtrlUses(id, blas);
        draft.retargetBranchArmAnchors(id, blas, blas);
        if (draft.outputNode() == id) {
            draft.setOutputNode(blas);
        }
        if (draft.returnNode() == id) {
            draft.setReturnNode(blas, draft.returnKind());
        }
        draft.eraseNode(id);
        ++count;
    }
    return count;
}

} // namespace

GCGraph *TensorFusePass::apply(GCGraph *graph, std::ostream &) {
    RuntimeGraphDraftSession session(context_, graph);
    size_t fusedCount = 0, libraryCount = 0;
    for (GCGraph *g : session.collectReachableRuntimeGraphs()) {
        GraphDraft &draft = session.edit(g);
        while (const auto m = findMatch(draft)) {
            fuse(draft, *m);
            ++fusedCount;
        }
        libraryCount += useLibraryGemm(draft);
    }
    CAMEL_LOG_INFO_S(
        "tensor::fuse",
        "fused {} matmul+add pattern(s), {} matmul(s) by the library GEMM",
        fusedCount,
        libraryCount);
    if (fusedCount == 0 && libraryCount == 0) {
        return graph;
    }
    session.commit();
    return context_->runtimeRootGraph();
}

void registerTensorPasses() {
    registerModulePass("tensor::fuse", [](const core::context::context_ptr_t &ctx) {
        return std::make_unique<TensorFusePass>(ctx);
    });
    // The declaration evaluator is a test (index_eval.h): it exists only when test passes are
    // enabled, as the core's test passes do (CAMEL_ENABLE_TEST_PASSES).
    if (std::getenv("CAMEL_ENABLE_TEST_PASSES") != nullptr) {
        registerModulePass("tensor::test_index_eval", [](const core::context::context_ptr_t &ctx) {
            return std::make_unique<IndexEvalTestPass>(ctx);
        });
    }
}

} // namespace camel::tensor::passes
