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
 * Created: Oct. 21, 2026
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The loops of a program, brought into the graph that holds the LOOP node.
 *
 * The front end builds the condition and the body of a loop as graphs of their own, which the LOOP
 * node names and passes the values they capture as With inputs. This copies each of them into the
 * graph with the LOOP node as a LoopCond / LoopBody region: its state ports are the ports of the
 * region, the values it captured are read where they are, and the result of the region is the exit
 * of the graph it came from. The LOOP node then names no graphs; its With inputs are the results
 * of its regions (the body first, then the condition), which is how what the regions compute is
 * reached from the node, as the JOIN reaches the results of the arms.
 */
#include "camel/passes/opt/inline.h"

#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft_clone.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/loop.h"

namespace camel::passes::opt {

using namespace camel::runtime;

namespace {

/// A LOOP node of `draft` that still names its condition or body graph, or kInvalidNodeRef.
gc_node_ref_t findGraphLoop(const GraphDraft &draft) {
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        const DraftNodeHeader *header = draft.header(id);
        if (header != nullptr && header->kind == GCNodeKind::Loop && draft.alive(id) &&
            isValid(draft.loopGraphsOf(id).bodyGraph)) {
            return id;
        }
    }
    return kInvalidNodeRef;
}

/// Copies `part`, the condition or the body graph of the LOOP node `loop`, into `draft` as the
/// region of kind `kind`.
gc_node_ref_t embedPart(
    GraphDraft &draft, gc_node_ref_t loop, const GCGraph *part, GCRegionKind kind,
    std::span<const gc_node_ref_t> captured) {
    const uint32_t region =
        draft.addRegion(kind, draft.nodeRegion(loop), loop, kind == GCRegionKind::LoopCond ? 0 : 1);
    const DraftGraphCloneResult cloned = cloneRuntimeGraphIntoDraft(draft, part, region);
    const auto toDraft = [&](gc_node_ref_t ref) { return cloned.sourceToCloned[ref]; };

    // The values the graph captured are read where the LOOP node reads them.
    const auto capturePorts = part->withPorts().empty() ? part->closureNodes() : part->withPorts();
    for (size_t c = 0; c < capturePorts.size() && c < captured.size(); ++c) {
        const gc_node_ref_t port = toDraft(capturePorts[c]);
        draft.replaceAllValueUses(port, captured[c]);
        draft.eraseNode(port);
    }
    std::vector<gc_node_ref_t> ports;
    for (gc_node_ref_t port : part->normPorts()) {
        ports.push_back(toDraft(port));
    }
    draft.setRegionPorts(region, ports);
    // The names of the ports are the declared arguments of the graph it came from.
    const auto norm  = part->normPorts();
    const auto names = part->funcType() != nullptr ? part->funcType()->argNames()
                                                    : std::vector<std::string_view>{};
    if (names.size() >= norm.size()) {
        camel::core::type::FunctionTypeFactory params;
        for (size_t i = 0; i < norm.size(); ++i) {
            params.addNormArg(
                std::string(names[names.size() - norm.size() + i]),
                part->node(norm[i])->dataType(),
                false);
        }
        params.setExitType(camel::core::type::Type::Void());
        draft.setRegionParams(region, params.build());
    }
    draft.setRegionResult(region, toDraft(part->exitNodeRef()));
    return draft.region(region).result;
}

} // namespace

const Program *regionizeLoops(
    const camel::core::context::context_ptr_t &context, const Program *program, GCGraph *root,
    bool detached) {
    RuntimeGraphDraftSession session(context, program, root != nullptr ? root : program->root());
    bool changed = false;
    for (GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        bool hasLoop = false;
        for (auto it = graph->nodes().begin(); it != graph->nodes().end() && !hasLoop; ++it) {
            hasLoop = graph->node(it.ref())->kind == GCNodeKind::Loop;
        }
        if (!hasLoop) {
            continue;
        }
        GraphDraft &draft = session.edit(graph);
        std::vector<GraphId> loopGraphs;
        for (gc_node_ref_t loop = findGraphLoop(draft); loop != kInvalidNodeRef;
             loop               = findGraphLoop(draft)) {
            const GCLoopBody graphs = draft.loopGraphsOf(loop);
            const std::vector<gc_node_ref_t> captured(
                draft.withInputsOf(loop).begin(),
                draft.withInputsOf(loop).end());
            const GCGraph *cond = isValid(graphs.condGraph) ? session.work().graph(graphs.condGraph)
                                                            : nullptr;
            const GCGraph *body = session.work().graph(graphs.bodyGraph);
            ASSERT(body != nullptr, "A LOOP node names a body graph that the program does not hold.");
            loopGraphs.push_back(graphs.bodyGraph);
            std::vector<gc_node_ref_t> results;
            results.push_back(embedPart(draft, loop, body, GCRegionKind::LoopBody, captured));
            if (cond != nullptr) {
                loopGraphs.push_back(graphs.condGraph);
                results.push_back(embedPart(draft, loop, cond, GCRegionKind::LoopCond, captured));
            }
            draft.setWithInputs(loop, results);
            draft.setLoopGraphs(loop, kInvalidGraphId, kInvalidGraphId);
            changed = true;
        }
        draft.dropUncalledGraphs(loopGraphs);
    }
    return changed ? session.commit(detached) : program;
}

} // namespace camel::passes::opt
