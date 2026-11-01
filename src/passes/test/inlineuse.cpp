// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The two readings `inline_use` and `inline_use_drop` print, and what they are for: one shows the
 * ambient use a copy kept, the other takes it away so the boundary check has something to refuse.
 */

#include "camel/passes/test/inlineuse.h"

#include "camel/core/resource_semantics.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/resources.h"

#include <algorithm>
#include <map>
#include <ostream>
#include <string>
#include <vector>

namespace {

using namespace camel::runtime;

constexpr std::string_view kPrintUri = ":io/println";

/// Every standard ambient use of the program, as `name (way)`, counted.
std::map<std::string, size_t> ambientUses(const camel::runtime::Program *program) {
    std::map<std::string, size_t> counts;
    for (GCGraph *graph : program->graphs()) {
        for (gc_node_ref_t id = 0; id < graph->nodeCount(); ++id) {
            if (graph->node(id) == nullptr) {
                continue; // a dead place: its id is kept so the others do not move
            }
            const NodeResources resources = resourcesOf(*program, *graph, id);
            for (const NodeAccess &access : resources.accesses) {
                if (access.resource.cls != camel::core::sem::ResourceClass::Ambient) {
                    continue;
                }
                if (!camel::core::sem::isStandardResource(access.resource.name)) {
                    continue;
                }
                std::string use = access.resource.name;
                use += " (";
                use.append(camel::core::sem::accessModeName(access.mode));
                use += ")";
                counts[use] += 1;
            }
        }
    }
    return counts;
}

} // namespace

const camel::runtime::Program *
InlineUseTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr) {
        return program;
    }
    const std::map<std::string, size_t> counts = ambientUses(program);
    os << "INLINE_USE kinds=" << counts.size();
    for (const auto &[use, count] : counts) {
        os << " " << use << "=" << count;
    }
    os << "\n";
    // One line per print node, so a copy inlining made is visible as a node of its own rather than
    // folded into a total: the total alone cannot say whether a print moved house or was doubled.
    for (GCGraph *graph : program->graphs()) {
        for (gc_node_ref_t id = 0; id < graph->nodeCount(); ++id) {
            if (graph->node(id) == nullptr) {
                continue;
            }
            const NodeResources resources = resourcesOf(*program, *graph, id);
            std::string uses;
            for (const NodeAccess &access : resources.accesses) {
                if (access.resource.cls != camel::core::sem::ResourceClass::Ambient ||
                    !camel::core::sem::isStandardResource(access.resource.name)) {
                    continue;
                }
                if (!uses.empty()) {
                    uses += ",";
                }
                uses += access.resource.name;
                uses += "/";
                uses.append(camel::core::sem::accessModeName(access.mode));
            }
            if (uses.empty()) {
                continue;
            }
            os << "INLINE_USE node=" << camel::runtime::toValue(graph->id()) << ":" << id << " uses=[" << uses << "]\n";
        }
    }
    return program;
}

const camel::runtime::Program *
InlineUseDropTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    size_t erased = 0;
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
            draft.eraseNode(print);
            ++erased;
        }
    }
    os << "INLINE_USE_DROP erased=" << erased << "\n";
    return session.commit();
}