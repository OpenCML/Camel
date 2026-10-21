/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Reify a validated operator region as an ordinary Camel function draft.
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
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
        for (auto input : source.withInputsOf(ref))
            captures.push_back(mapped(input));
        // Frozen LOOP references are indices into the source's reference table: the draft
        // has its own, so the payload is made again.
        std::optional<rt::GCLoopRef> loop;
        auto payload = source.nodePayloadBytes(ref);
        if (node->kind == rt::GCNodeKind::Loop) {
            const rt::GCLoopBody graphs = rt::loopBodyOf(source, ref);
            if (rt::isValid(graphs.condGraph))
                draft.addDependency(graphs.condGraph);
            draft.addDependency(graphs.bodyGraph);
            loop = rt::GCLoopRef{
                .kind            = graphs.kind,
                .condRef         = draft.payloadGraphIndexOf(graphs.condGraph),
                .bodyRef         = draft.payloadGraphIndexOf(graphs.bodyGraph),
                .collectionCount = graphs.collectionCount,
            };
            payload = std::as_bytes(std::span(&*loop, 1));
        }
        auto *type              = typeOf(source, id);
        result.sourceToNode[id] = draft.addNode(
            {.dataIndex    = draft.allocateRuntimeSlot(type),
             .dataType     = type,
             .kind         = node->kind,
             .runtimeFlags = node->flags,
             .origin       = source.nodeOrigin(ref),
             .permissions  = source.nodePermissions(ref),
             .payload      = payload,
             .normInputs   = inputs,
             .withInputs   = captures});
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
