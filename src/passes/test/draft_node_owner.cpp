// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The witness of the draft's identity: a node handle outlives the builder that made it, the address
 * is handed to a later builder, and the handle is asked who it belongs to.
 *
 * A `DraftNodeRef` names its draft by the address *and* the epoch the draft was made with
 * (`include/camel/runtime/draft.h#isNamedBy`), because the address alone stops naming anything the
 * moment the allocator hands it to the next draft. Without the epoch the answer here is the wrong
 * graph and nothing says so: the pass prints the two answers side by side, so a reader who deletes
 * the epoch sees this case turn red instead of a comment nobody triggers.
 *
 * The control comes first. A node of the graph that holds the address must still resolve to that
 * graph; a refusal that also refuses the right handle would be a worse defect than the one guarded.
 */

#include "camel/passes/test/draft_node_owner.h"

#include "../../compile/gir/builder_support.h"

#include "camel/compile/gir/draft_graph_builder.h"
#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph_txn.h"

#include <format>
#include <memory>
#include <string_view>
#include <vector>

using namespace camel::compile::gir;

namespace {

/// The reading of one step. It is the pass's output, so the plan can assert it: a line a person can
/// read in the log is worth more than a boolean nobody prints.
void say(std::ostream &os, std::string_view line) { os << "[DRAFT_NODE_OWNER] " << line << "\n"; }

} // namespace

const camel::runtime::Program *
DraftNodeOwnerTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    const auto make = []() {
        return std::make_shared<DraftGraphBuilder>(camel::runtime::GraphTransaction::open(
            static_cast<camel::runtime::GraphId>(1), camel::core::type::FunctionType::create()));
    };

    // A builder, and a handle taken out of it: node 0 of its draft, a Copy.
    compile_graph_ptr_t builderA = make();
    const node_handle_t stale     = builderA->addCopyNode(camel::core::type::Type::Float32());
    const void *staleDraft        = static_cast<const void *>(stale.draft);
    const int kindWhenMade        = static_cast<int>(nodeKindOf(stale));
    // The builder goes: nothing owns that address any more, and the handle is all that is left.
    builderA.reset();

    // Something that lands on the address. Node 0 of the new draft is a Gate, not a Copy, so a
    // handle read through the new draft is visibly the wrong node rather than a matching one.
    std::vector<compile_graph_ptr_t> alive;
    DraftGraphBuilder *builderB = nullptr;
    node_handle_t liveB;
    std::size_t attempt = 0;
    for (std::size_t i = 0; i < 64 && builderB == nullptr; ++i) {
        compile_graph_ptr_t candidate = make();
        const node_handle_t candidateNode =
            candidate->addGateNode(camel::core::type::Type::Float32());
        alive.push_back(candidate);
        if (static_cast<const void *>(&candidate->view()) == staleDraft) {
            builderB = candidate.get();
            liveB    = candidateNode;
            attempt  = i + 1;
        }
    }
    if (builderB == nullptr) {
        // Not a silent pass: the case this pass exists for did not happen, and a witness that
        // cannot set up its case must say so instead of reporting nothing wrong.
        say(os, "MISSING the allocator never handed the address to a later draft");
        return program;
    }

    // The control: a node of the graph that holds the address resolves to that graph.
    const bool liveOwned = DraftGraphBuilder::fromRef(liveB) == builderB;
    say(os, std::format("live_owner={}", liveOwned ? "yes" : "no"));

    // The case: the same address, a different draft. `same_address=1` is the point of the whole
    // line -- the address alone answers "this graph", and the epoch is the only thing that says no.
    const bool sameAddress = stale.draft == &builderB->view();
    say(os, std::format("reused=1 attempt={} same_address={}", attempt, sameAddress ? 1 : 0));

    const bool staleIsNamed    = builderB->view().isNamedBy(stale);
    const bool staleHasOwner   = DraftGraphBuilder::fromRef(stale) != nullptr;
    const int kindAtTheAddress = static_cast<int>(nodeKindOf(liveB));
    say(os, std::format("kind_when_made={} kind_at_the_address_now={}", kindWhenMade, kindAtTheAddress));
    say(
        os,
        std::format(
            "stale_is_named={} stale_owner={}",
            staleIsNamed ? 1 : 0,
            staleHasOwner ? "live" : "none"));
    if (!liveOwned || staleIsNamed || staleHasOwner || !sameAddress) {
        say(os, "MISSING the stale handle resolved to the draft that holds its address");
        return program;
    }
    // The refusal, in the words the guard would use: a handle names the draft it was made in, by
    // the epoch, and the address it was made at now holds a draft of another epoch.
    say(
        os,
        std::format(
            "Refused: handle made in epoch {} at {}, and the address now holds epoch {}",
            stale.epoch,
            staleDraft,
            builderB->view().epoch()));
    say(os, "ok");
    return program;
}