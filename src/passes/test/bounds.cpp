// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Test-only producer of one class of boundary violation: a pass that keeps every ambient resource
 * name the program had and changes only the way it is used -- `stdout` read where the program wrote
 * it. It stands where the print stood, so nothing else about the program moves: the name set is the
 * same before and after, which is what the check compared, and the access is not.
 */

#include "camel/passes/test/bounds.h"

#include "../opt/generic/rewrite.h"

#include "camel/core/operator_table.h"
#include "camel/core/resource_semantics.h"
#include "camel/core/type/base.h"
#include "camel/runtime/draft_session.h"

#include <ostream>
#include <span>
#include <string_view>
#include <vector>

namespace {

using namespace camel::runtime;

using camel::core::OperatorTable;
using camel::core::sem::AccessMode;
using camel::core::sem::externalAmbient;
using camel::core::type::Type;

/// The print, and the read of the same resource that takes its place.
constexpr std::string_view kPrintUri = ":io/println";
constexpr std::string_view kReadUri  = "test::bounds:stdout_read";

/// Declares the read once: never twice, since a second declaration of a uri is a defect.
void declareRead() {
    static bool declared = false;
    if (declared) {
        return;
    }
    OperatorTable::instance().set(std::string(kReadUri), camel::core::OperatorTraits{.pure = false});
    OperatorTable::instance().declareResources(
        std::string(kReadUri), externalAmbient("stdout", AccessMode::Read));
    declared = true;
}

} // namespace

const camel::runtime::Program *
BoundsFlipTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr) {
        return program;
    }
    declareRead();
    RuntimeGraphDraftSession session(context_, program);
    size_t flipped = 0;
    for (GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        auto &transaction = session.edit(graph);
        auto &draft       = transaction.edit();
        std::vector<gc_node_ref_t> prints;
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (!draft.alive(id)) {
                continue;
            }
            const DraftNodeHeader *header = draft.header(id);
            if (header == nullptr || header->kind != GCNodeKind::Oper) {
                continue;
            }
            const std::span<const std::byte> payload = draft.payloadOf(id);
            if (payload.size() < sizeof(GCOperBody)) {
                continue;
            }
            if (reinterpret_cast<const GCOperBody *>(payload.data())->uri() == kPrintUri) {
                prints.push_back(id);
            }
        }
        for (gc_node_ref_t print : prints) {
            const std::vector<gc_node_ref_t> inputs(draft.normInputsOf(print).begin(),
                                                    draft.normInputsOf(print).end());
            const gc_node_ref_t read = draft.addOperNode(Type::Void(), nullptr, kReadUri);
            draft.setNormInputs(read, std::span<const gc_node_ref_t>(inputs.begin(), inputs.size()));
            camel::passes::generic::replaceNode(transaction, print, read);
            ++flipped;
        }
    }
    // The reading the class is counted with: the name the program had and the two ways of using it.
    os << "BOUNDS_FLIP flipped=" << flipped << " kind=ambient_use name=stdout before=write"
       << " after=read\n";
    return session.commit();
}