// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Reify a validated operator region as an ordinary Camel function draft.

#include "camel/core/mm/gen.h"
#include "camel/core/tags.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/loop.h"
#include "tiles/internal/graph_values.h"
#include "tiles/internal/regions.h"
#include <optional>
#include <stdexcept>
#include <unordered_set>

namespace camel::tiles {
namespace rt = runtime;
namespace ty = core::type;
namespace {
void require(bool ok, const char *reason) {
    if (!ok)
        throw std::runtime_error(std::string("region extraction: ") + reason);
}
ty::Type *typeOf(const rt::GCGraph &source, size_t id) {
    const auto *node = source.node(static_cast<rt::gc_node_ref_t>(id));
    require(node != nullptr, "node outside source graph");
    if (auto *type = node->dataType())
        return type;
    if (node->dataIndex < 0 && source.staticDataType())
        return source.staticDataType()->typeAt(static_cast<size_t>(-node->dataIndex));
    throw std::runtime_error("region extraction: missing value type");
}
} // namespace
namespace {
/// The nodes in the condition and the body regions of the LOOP node `loop`, in node order.
std::vector<rt::gc_node_ref_t> loopInternals(const rt::GCGraph &source, rt::gc_node_ref_t loop) {
    const uint32_t cond = source.loopRegionOf(loop, rt::GCRegionKind::LoopCond);
    const uint32_t body = source.loopRegionOf(loop, rt::GCRegionKind::LoopBody);
    std::vector<rt::gc_node_ref_t> nodes;
    const rt::gc_node_ref_t round = source.withInputsOf(loop).back(); // the loop's own port
    for (rt::gc_node_ref_t ref = 0; ref < source.nodeCount(); ++ref) {
        if (!source.containsNodeRef(ref) || source.node(ref) == nullptr) {
            continue;
        }
        if (ref == round) {
            nodes.push_back(ref);
            continue;
        }
        for (uint32_t at = source.nodeRegion(ref); at != 0; at = source.region(at)->parent) {
            if (at == cond || at == body) {
                nodes.push_back(ref);
                break;
            }
        }
    }
    return nodes;
}

/// Copies the nodes of the regions of the LOOP node `loop` into `draft` (their inputs from outside
/// are the nodes `sourceToNode` has), and gives `sourceToNode` the copies.
std::vector<rt::gc_node_ref_t> copyLoopNodes(
    const rt::GCGraph &source, rt::GraphDraft &draft, std::vector<rt::gc_node_ref_t> &sourceToNode,
    rt::gc_node_ref_t loop) {
    const auto nodes = loopInternals(source, loop);
    for (auto ref : nodes) {
        const auto *node = source.node(ref);
        require(
            node->kind == rt::GCNodeKind::Oper || node->kind == rt::GCNodeKind::Accs ||
                node->kind == rt::GCNodeKind::Fill || node->kind == rt::GCNodeKind::Cast ||
                node->kind == rt::GCNodeKind::Port || node->kind == rt::GCNodeKind::Gate,
            "unsupported node in a loop");
        auto *type = typeOf(source, ref);
        if (node->kind == rt::GCNodeKind::Port) {
            sourceToNode[ref] = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
        } else {
            sourceToNode[ref] = draft.addNode(
                {.dataIndex    = draft.allocateRuntimeSlot(type),
                 .dataType     = type,
                 .kind         = node->kind,
                 .runtimeFlags = node->flags,
                 .origin       = source.nodeOrigin(ref),
                 .permissions  = camel::core::tag::factsOf(source, ref).permissions,
                 .payload      = source.nodePayloadBytes(ref)});
        }
        draft.setNodeOrigin(sourceToNode[ref], source.nodeOrigin(ref));
    }
    const auto mappedInput = [&](rt::gc_node_ref_t input) {
        require(
            input < sourceToNode.size() && sourceToNode[input] != rt::kInvalidNodeRef,
            "a loop reads a node the region does not hold");
        return sourceToNode[input];
    };
    for (auto ref : nodes) {
        std::vector<rt::gc_node_ref_t> norm, with, ctrl;
        for (auto input : source.normInputsOf(ref))
            norm.push_back(mappedInput(input));
        for (auto input : source.withInputsOf(ref))
            with.push_back(mappedInput(input));
        for (auto input : source.ctrlInputsOf(ref))
            ctrl.push_back(mappedInput(input));
        draft.setNormInputs(sourceToNode[ref], norm);
        draft.setWithInputs(sourceToNode[ref], with);
        draft.setCtrlInputs(sourceToNode[ref], ctrl);
    }
    return nodes;
}

/// Makes the regions of the copy of the LOOP node `loop`: the copied nodes are in them.
void makeLoopRegions(
    const rt::GCGraph &source, rt::GraphDraft &draft,
    const std::vector<rt::gc_node_ref_t> &sourceToNode, rt::gc_node_ref_t loop) {
    const auto target = sourceToNode[loop];
    for (auto kind : {rt::GCRegionKind::LoopCond, rt::GCRegionKind::LoopBody}) {
        const uint32_t from = source.loopRegionOf(loop, kind);
        if (from == 0) {
            continue;
        }
        const uint32_t made =
            draft.addRegion(kind, 0, target, kind == rt::GCRegionKind::LoopCond ? 0 : 1);
        std::vector<rt::gc_node_ref_t> ports;
        for (auto port : source.regionPorts(from))
            ports.push_back(sourceToNode[port]);
        draft.setRegionPorts(made, ports);
        draft.setRegionParams(made, source.region(from)->params);
        for (auto ref : loopInternals(source, loop)) {
            for (uint32_t at = source.nodeRegion(ref); at != 0; at = source.region(at)->parent) {
                if (at == from) {
                    draft.setNodeRegion(sourceToNode[ref], made);
                    break;
                }
            }
        }
    }
}
} // namespace

RegionDraft extractRegion(const rt::GCGraph &source, const Region &region) {
    require(!region.members.empty() && !region.outputs.empty(), "empty region or result");
    RegionDraft result{
        std::make_unique<rt::GraphDraft>(),
        {},
        std::vector<rt::gc_node_ref_t>(source.nodeCount(), rt::kInvalidNodeRef)};
    auto &draft = *result.graph;
    std::unordered_set<size_t> members(region.members.begin(), region.members.end());
    require(members.size() == region.members.size(), "duplicate member");
    for (auto id : members)
        require(id < source.nodeCount(), "member outside source graph");
    auto mapped = [&](size_t id) {
        require(
            id < result.sourceToNode.size() && result.sourceToNode[id] != rt::kInvalidNodeRef,
            "missing input or members not in dependency order");
        return result.sourceToNode[id];
    };
    ty::FunctionTypeFactory signature;
    for (auto id : region.inputs) {
        require(!members.contains(id), "member listed as input");
        auto *type = typeOf(source, id);
        require(result.sourceToNode[id] == rt::kInvalidNodeRef, "duplicate input");
        const auto *node = source.node(static_cast<rt::gc_node_ref_t>(id));
        rt::gc_node_ref_t target;
        if (node->kind == rt::GCNodeKind::Data && node->dataIndex < 0) {
            const auto slot = static_cast<size_t>(-node->dataIndex);
            require(slot < source.staticSlots().size(), "invalid constant slot");
            target = draft.materializeStaticValue(source.staticSlots()[slot], type, node->flags);
        } else {
            target = draft.addPortNode(type, draft.allocateRuntimeSlot(type));
            draft.appendNormPort(target);
            signature.addNormArg("__tiles_input_" + std::to_string(id) + "__", type, false);
            result.parameters.push_back(id);
        }
        draft.setNodeOrigin(target, source.nodeOrigin(static_cast<rt::gc_node_ref_t>(id)));
        result.sourceToNode[id] = target;
    }
    for (auto id : region.members) {
        const auto ref   = static_cast<rt::gc_node_ref_t>(id);
        const auto *node = source.node(ref);
        require(
            node->kind == rt::GCNodeKind::Oper || node->kind == rt::GCNodeKind::Loop ||
                node->kind == rt::GCNodeKind::Accs,
            "unsupported region member");
        require(
            source.ctrlInputsOf(ref).empty() && source.ctrlOutputsOf(ref).empty(),
            "control endpoint in region");
        require(
            node->kind == rt::GCNodeKind::Loop || source.withInputsOf(ref).empty(),
            "context arguments in region");
        std::vector<rt::gc_node_ref_t> inputs;
        for (auto input : source.normInputsOf(ref))
            inputs.push_back(mapped(input));
        std::vector<rt::gc_node_ref_t> captures;
        // The condition and the body of a loop are regions of the graph: their nodes are copied
        // into the draft first, the LOOP node takes their results as its With inputs.
        std::vector<rt::gc_node_ref_t> loopNodes;
        if (node->kind == rt::GCNodeKind::Loop) {
            loopNodes = copyLoopNodes(source, draft, result.sourceToNode, ref);
            for (auto input : source.withInputsOf(ref))
                captures.push_back(mapped(input));
        } else {
            for (auto input : source.withInputsOf(ref))
                captures.push_back(mapped(input));
        }
        auto *type              = typeOf(source, id);
        result.sourceToNode[id] = draft.addNode(
            {.dataIndex    = draft.allocateRuntimeSlot(type),
             .dataType     = type,
             .kind         = node->kind,
             .runtimeFlags = node->flags,
             .origin       = source.nodeOrigin(ref),
             .permissions  = camel::core::tag::factsOf(source, ref).permissions,
             .payload      = source.nodePayloadBytes(ref),
             .normInputs   = inputs,
             .withInputs   = captures});
        if (node->kind == rt::GCNodeKind::Loop) {
            makeLoopRegions(source, draft, result.sourceToNode, ref);
        }
    }
    std::vector<rt::gc_node_ref_t> outputs;
    std::vector<ty::Type *> types;
    std::unordered_set<size_t> unique;
    for (auto id : region.outputs) {
        require(members.contains(id) && unique.insert(id).second, "invalid or duplicate result");
        outputs.push_back(mapped(id));
        types.push_back(typeOf(source, id));
    }
    auto output      = outputs.front();
    auto *resultType = types.front();
    if (outputs.size() > 1) {
        output     = packGraphTuple(draft, outputs);
        resultType = draft.header(output)->dataType;
    }
    signature.setExitType(resultType);
    draft.setFuncType(signature.build());
    draft.setOutputNode(output);
    draft.setExitNode(output);
    draft.setReturnNode(output, rt::GCReturnKind::Self);
    return result;
}
} // namespace camel::tiles
