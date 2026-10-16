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

/* Draft-only topology tests: no synthetic operator payload is executed or encoded. */
#include "camel/passes/test/control.h"
#include "camel/runtime/control.h"

#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
using namespace camel::runtime;

gc_node_ref_t node(GraphDraft &draft, std::initializer_list<gc_node_ref_t> predecessors = {}) {
    return draft.addNode(
        DraftNodeInit{
            .kind       = GCNodeKind::Oper,
            .ctrlInputs = std::span<const gc_node_ref_t>(predecessors.begin(), predecessors.size()),
        });
}
void check(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}
void chainAndBudget() {
    GraphDraft draft;
    const auto a = node(draft), b = node(draft, {a}), c = node(draft, {b});
    const auto d = node(draft, {a, c});
    check(!hasControlDependency(draft, a, c, 0), "zero-budget query must be conservative");
    check(reduceControlDependencies(draft, 0) == 0, "zero-budget reduction changed edges");
    check(reduceControlDependencies(draft, 1) == 0, "incomplete search removed an unproven edge");
    const std::vector<gc_node_ref_t> before{a, b, c};
    const auto frontier = controlFrontier(draft, before);
    check(frontier.size() == 1 && frontier.front() == c, "rewrite frontier retained ancestors");
    check(reduceControlDependencies(draft) == 1, "transitive CTRL edge was not removed");
    check(
        draft.ctrlInputsOf(d).size() == 1 && draft.ctrlInputsOf(d).front() == c,
        "latest predecessor was not retained");
    check(!appendControlDependency(draft, a, d), "rewrite reinserted an implied edge");
    check(reduceControlDependencies(draft) == 0, "small graph reduction is not idempotent");
}
void dataIsNotAControlProof() {
    GraphDraft draft;
    const auto a = node(draft), b = node(draft);
    draft.setNormInputs(b, std::span<const gc_node_ref_t>(&a, 1));
    node(draft, {a, b});
    check(reduceControlDependencies(draft) == 0, "a data path was used as a CTRL-only proof");
}
void branchBoundaries() {
    GraphDraft draft;
    const auto a = node(draft);
    DraftBrchPayload body{.armCount = 1};
    std::vector<std::byte> bytes(sizeof(body) + sizeof(GCBranchArm));
    std::memcpy(bytes.data(), &body, sizeof(body));
    const auto branch = draft.addNode(
        DraftNodeInit{
            .kind       = GCNodeKind::Brch,
            .payload    = bytes,
            .ctrlInputs = std::span<const gc_node_ref_t>(&a, 1),
        });
    const auto other = node(draft, {a});
    const auto head  = node(draft, {branch, a, other});
    GCBranchArm arm{.head = head, .tail = head};
    std::memcpy(draft.mutablePayloadOf(branch).data() + sizeof(body), &arm, sizeof(arm));
    const auto tail = node(draft, {a, head});
    check(reduceControlDependencies(draft) == 0, "branch anchor or boundary was reduced");
    check(
        draft.ctrlInputsOf(head).size() == 3 && draft.ctrlInputsOf(tail).size() == 2,
        "branch controls changed");
}
void denseChain() {
    GraphDraft draft;
    std::vector<gc_node_ref_t> predecessors;
    constexpr size_t count = 128;
    for (size_t i = 0; i < count; ++i) {
        const auto current = draft.addNode(
            DraftNodeInit{
                .kind       = GCNodeKind::Oper,
                .ctrlInputs = predecessors,
            });
        predecessors.push_back(current);
    }
    check(reduceControlDependencies(draft) == 8001, "dense CTRL chain did not reduce to its spine");
    for (size_t i = 1; i < count; ++i) {
        check(
            draft.ctrlInputsOf(predecessors[i]).size() == 1 &&
                draft.ctrlInputsOf(predecessors[i]).front() == predecessors[i - 1],
            "dense chain lost its immediate predecessor");
    }
}
} // namespace

camel::runtime::GCGraph *ControlTestPass::apply(camel::runtime::GCGraph *graph, std::ostream &os) {
    chainAndBudget();
    dataIsNotAControlProof();
    branchBoundaries();
    denseChain();
    os << "CONTROL topology, budgets, branches, dense chain: PASS\n";
    return graph;
}
