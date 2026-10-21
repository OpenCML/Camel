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
 * The loops of a program as graphs of their own, for the code that reads a loop as two graphs
 * (autodiff).
 *
 * The condition and the body of a loop are regions of the graph that has the LOOP node. This
 * outlines each into a graph: the state ports of the region are the norm ports of the graph, the
 * values the two regions read from outside are with-ports of both (one capture table, in the
 * order of the node's With inputs). The LOOP node then names the two graphs. The result is a
 * private program (detached), not the one of the context.
 */
#include "camel/runtime/loop_outline.h"

#include "camel/runtime/draft_clone.h"
#include "camel/runtime/draft_session.h"

#include <algorithm>
#include <unordered_map>

namespace camel::runtime {

using camel::core::type::FunctionType;
using camel::core::type::Type;

namespace {

uint32_t depthOf(const GraphDraft &draft, uint32_t region) {
    uint32_t depth = 0;
    while (region != 0) {
        region = draft.region(region).parent;
        ++depth;
    }
    return depth;
}

void appendUnique(std::vector<gc_node_ref_t> &list, gc_node_ref_t ref) {
    if (std::ranges::find(list, ref) == list.end()) {
        list.push_back(ref);
    }
}

/// The nodes of `draft` in the region `region` or a region in it.
std::vector<gc_node_ref_t> membersOf(const GraphDraft &draft, uint32_t region) {
    std::vector<gc_node_ref_t> members;
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
        if (draft.alive(id) && draft.nodeRegion(id) != 0 &&
            draft.regionWithin(draft.nodeRegion(id), region)) {
            members.push_back(id);
        }
    }
    return members;
}

/// What a part of a loop (its condition, its body) reads from outside, and what its statements
/// wait for.
struct PartInputs {
    std::vector<uint8_t> member;
    std::vector<gc_node_ref_t> members;
    std::vector<gc_node_ref_t> externals;
    std::vector<gc_node_ref_t> ctrlPreds;
    bool readFromOutside = false; // a node of the part is read by a node that is not
};

PartInputs inputsOf(
    const GraphDraft &draft, uint32_t region, gc_node_ref_t loop, gc_node_ref_t result) {
    PartInputs part;
    part.member.assign(draft.nodeSlotCount(), 0);
    part.members = membersOf(draft, region);
    for (gc_node_ref_t id : part.members) {
        part.member[id] = 1;
    }
    const auto isConstant = [&](gc_node_ref_t id) {
        return draft.header(id)->kind == GCNodeKind::Data;
    };
    const auto external = [&](gc_node_ref_t input) {
        if (input != kInvalidNodeRef && !part.member[input] && !isConstant(input)) {
            appendUnique(part.externals, input);
        }
    };
    for (gc_node_ref_t id : part.members) {
        for (auto inputs : {draft.normInputsOf(id), draft.withInputsOf(id)}) {
            for (gc_node_ref_t input : inputs) {
                external(input);
            }
        }
        for (gc_node_ref_t pred : draft.ctrlInputsOf(id)) {
            if (!part.member[pred] && pred != loop) {
                appendUnique(part.ctrlPreds, pred);
            }
        }
        // Nothing but the node reads a node of the part, and that only its result.
        for (auto users : {draft.normUsersOf(id), draft.withUsersOf(id)}) {
            for (gc_node_ref_t user : users) {
                part.readFromOutside =
                    part.readFromOutside || (!part.member[user] && !(user == loop && id == result));
            }
        }
    }
    external(result);
    return part;
}

/// Outlines the part of the LOOP node `loop` that lies in the region `region` (of kind `kind`)
/// into a new member of `group`. `externals` are the values of the owner the graph takes as
/// with-ports, in order.
GraphId outlinePart(
    GraphDraft &draft, GraphDraftGroup &group, gc_node_ref_t loop, GCRegionKind kind,
    gc_node_ref_t result, const PartInputs &part, const std::vector<gc_node_ref_t> &externals,
    const std::string &mangledName, const std::string &name) {
    const GraphId id = group.reserve(mangledName, name);
    group.define(id, std::make_unique<GraphDraft>());
    GraphDraft &graph = group.draft(id);
    const size_t slots = draft.nodeSlotCount();

    DraftGraphCloneResult cloned = cloneDraftGraphIntoDraft(graph, draft, 0);
    const auto &toGraph          = cloned.sourceToCloned;
    const uint32_t copy          = graph.loopRegionOf(toGraph[loop], kind);
    const std::vector<gc_node_ref_t> ports(
        graph.regionPortsOf(copy).begin(),
        graph.regionPortsOf(copy).end());
    const camel::core::type::FunctionType *params = graph.region(copy).params;
    // The region is the body of the graph; the regions around it end.
    graph.flattenRegion(copy);

    const auto isConstant = [&](gc_node_ref_t node) {
        return draft.header(node)->kind == GCNodeKind::Data;
    };
    // The values read from outside become with-ports, one for every value of the capture table.
    std::unordered_map<gc_node_ref_t, gc_node_ref_t> portOf; // by the node in `graph`
    std::vector<gc_node_ref_t> withPorts;
    for (gc_node_ref_t input : externals) {
        Type *type               = draft.header(input)->dataType;
        const gc_node_ref_t port = graph.addPortNode(type, graph.allocateRuntimeSlot(type));
        graph.appendWithPort(port);
        portOf.emplace(toGraph[input], port);
        withPorts.push_back(port);
    }
    const auto remap = [&](std::span<const gc_node_ref_t> inputs) {
        std::vector<gc_node_ref_t> mapped(inputs.begin(), inputs.end());
        bool changed = false;
        for (gc_node_ref_t &input : mapped) {
            if (const auto it = portOf.find(input); it != portOf.end()) {
                input   = it->second;
                changed = true;
            }
        }
        return std::pair{std::move(mapped), changed};
    };
    for (gc_node_ref_t node : part.members) {
        const gc_node_ref_t inGraph = toGraph[node];
        if (auto [norm, changed] = remap(graph.normInputsOf(inGraph)); changed) {
            graph.setNormInputs(inGraph, norm);
        }
        if (auto [with, changed] = remap(graph.withInputsOf(inGraph)); changed) {
            graph.setWithInputs(inGraph, with);
        }
    }
    gc_node_ref_t anchor = kInvalidNodeRef;
    if (result != kInvalidNodeRef) {
        const gc_node_ref_t inGraph = toGraph[result];
        anchor                      = portOf.contains(inGraph) ? portOf.at(inGraph) : inGraph;
    }
    // What the part does not hold goes; the constants it reads stay, those nothing reads go too.
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        const gc_node_ref_t inGraph = toGraph[node];
        if (inGraph != kInvalidNodeRef && graph.alive(inGraph) && !part.member[node] &&
            !isConstant(node) && !portOf.contains(inGraph)) {
            graph.eraseNode(inGraph);
        }
    }
    for (gc_node_ref_t node = 0; node < slots; ++node) {
        const gc_node_ref_t inGraph = toGraph[node];
        if (inGraph != kInvalidNodeRef && isConstant(node) && graph.alive(inGraph) &&
            inGraph != anchor && graph.normUsersOf(inGraph).empty() &&
            graph.withUsersOf(inGraph).empty() && graph.ctrlUsersOf(inGraph).empty()) {
            graph.eraseNode(inGraph);
        }
    }
    for (const auto &[original, port] : portOf) {
        if (graph.alive(original)) {
            graph.eraseNode(original);
        }
    }
    for (gc_node_ref_t port : ports) {
        graph.appendNormPort(port);
    }
    // The value is behind the GATEs that order it after the statements of the part.
    gc_node_ref_t value = anchor;
    for (int guard = 0; guard < 64; ++guard) {
        const DraftNodeHeader *header = graph.header(value);
        if (header == nullptr || header->kind != GCNodeKind::Gate ||
            graph.normInputsOf(value).empty()) {
            break;
        }
        value = graph.normInputsOf(value).front();
    }
    graph.setOutputNode(anchor);
    graph.setExitNode(anchor);
    graph.setReturnNode(value, GCReturnKind::Self);
    // A gate that waited on what the part does not hold waits on nothing.
    graph.dissolveUnorderedGates();
    Type *exitType = graph.header(value)->dataType;
    // The graph takes the ports of the region, named as the region names them, and the values it
    // read from outside.
    camel::core::type::FunctionTypeFactory signature;
    for (size_t i = 0; i < externals.size(); ++i) {
        signature.addWithArg("__capture" + std::to_string(i), graph.header(withPorts[i])->dataType, false);
    }
    const auto names = params != nullptr ? params->argNames() : std::vector<std::string_view>{};
    for (size_t i = 0; i < ports.size(); ++i) {
        signature.addNormArg(
            i < names.size() ? std::string(names[i]) : "__state" + std::to_string(i),
            graph.header(ports[i])->dataType,
            false);
    }
    signature.setExitType(exitType);
    graph.setFuncType(signature.build());
    return id;
}

/// Outlines the loop `loop` of `draft` into two graphs of `group`.
bool outlineLoop(
    GraphDraft &draft, GraphDraftGroup &group, gc_node_ref_t loop, const std::string &mangledBase,
    const std::string &nameBase) {
    const uint32_t condRegion = draft.loopRegionOf(loop, GCRegionKind::LoopCond);
    const uint32_t bodyRegion = draft.loopRegionOf(loop, GCRegionKind::LoopBody);
    if (bodyRegion == 0) {
        return false;
    }
    // The With inputs of the node are the results of its parts: the body's, then the condition's.
    const gc_node_ref_t bodyResult = draft.withInputsOf(loop)[0];
    const gc_node_ref_t condResult =
        condRegion != 0 ? draft.withInputsOf(loop)[1] : kInvalidNodeRef;
    PartInputs cond;
    if (condRegion != 0) {
        cond = inputsOf(draft, condRegion, loop, condResult);
    }
    PartInputs body = inputsOf(draft, bodyRegion, loop, bodyResult);
    if (cond.readFromOutside || body.readFromOutside) {
        return false;
    }
    // One capture table for both graphs.
    std::vector<gc_node_ref_t> externals = cond.externals;
    for (gc_node_ref_t input : body.externals) {
        appendUnique(externals, input);
    }
    for (gc_node_ref_t input : externals) {
        if (draft.header(input)->dataType == nullptr) {
            return false;
        }
    }
    GraphId condId = kInvalidGraphId;
    if (condRegion != 0) {
        condId = outlinePart(
            draft,
            group,
            loop,
            GCRegionKind::LoopCond,
            condResult,
            cond,
            externals,
            mangledBase + ".cond",
            nameBase + ".cond");
    }
    const GraphId bodyId = outlinePart(
        draft,
        group,
        loop,
        GCRegionKind::LoopBody,
        bodyResult,
        body,
        externals,
        mangledBase + ".body",
        nameBase + ".body");

    // The node names the graphs, passes what they read, and waits for what their statements did.
    if (isValid(condId)) {
        draft.addDependency(condId);
        draft.addSubGraph(condId);
    }
    draft.addDependency(bodyId);
    draft.addSubGraph(bodyId);
    std::vector<gc_node_ref_t> ctrl(draft.ctrlInputsOf(loop).begin(), draft.ctrlInputsOf(loop).end());
    for (const PartInputs *part : {&cond, &body}) {
        for (gc_node_ref_t pred : part->ctrlPreds) {
            appendUnique(ctrl, pred);
        }
    }
    for (const PartInputs *part : {&cond, &body}) {
        for (gc_node_ref_t node : part->members) {
            if (draft.alive(node)) {
                draft.eraseNode(node);
            }
        }
    }
    draft.setLoopGraphs(loop, condId, bodyId);
    draft.setWithInputs(loop, externals);
    draft.setCtrlInputs(loop, ctrl);
    for (uint32_t region : {condRegion, bodyRegion}) {
        if (region != 0) {
            draft.retireRegion(region);
        }
    }
    return true;
}

} // namespace

const Program *outlineLoopRegions(
    const camel::core::context::context_ptr_t &context, const Program *program, GCGraph *root) {
    if (program == nullptr || root == nullptr) {
        return program;
    }
    RuntimeGraphDraftSession session(context, program, root);
    std::vector<GCGraph *> withLoops;
    for (GCGraph *graph : session.collectReachableRuntimeGraphs()) {
        for (uint32_t r = 1; r < graph->regionCount(); ++r) {
            if (graph->region(r)->kind == GCRegionKind::LoopBody) {
                withLoops.push_back(graph);
                break;
            }
        }
    }
    if (withLoops.empty()) {
        return program;
    }
    GraphDraftGroup group(context, program);
    for (GCGraph *graph : withLoops) {
        GraphDraft &draft = session.edit(graph);
        // The loops inside loops first: the part that holds them then holds their nodes.
        std::vector<gc_node_ref_t> loops;
        for (gc_node_ref_t id = 0; id < draft.nodeSlotCount(); ++id) {
            if (draft.alive(id) && draft.header(id)->kind == GCNodeKind::Loop &&
                draft.loopRegionOf(id, GCRegionKind::LoopBody) != 0) {
                loops.push_back(id);
            }
        }
        std::ranges::stable_sort(loops, [&](gc_node_ref_t a, gc_node_ref_t b) {
            return depthOf(draft, draft.nodeRegion(a)) > depthOf(draft, draft.nodeRegion(b));
        });
        size_t count = 0;
        for (gc_node_ref_t loop : loops) {
            const std::string suffix = ".loop" + std::to_string(count + 1);
            if (outlineLoop(draft, group, loop, graph->mangledName() + suffix, graph->name() + suffix)) {
                ++count;
            }
        }
    }
    group.encode();
    for (GraphId id : group.ids()) {
        session.adoptGraph(group.encoded(id));
    }
    return session.commit(/*detached=*/true);
}

} // namespace camel::runtime
