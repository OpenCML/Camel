// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Extract eligible regions and replace them with typed calls, independent of device backend.

#include "camel/core/context/context.h"
#include "camel/core/error/diagnostics/messages/runtime.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/struct.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/control_regions.h"
#include "camel/runtime/draft_session.h"
#include "camel/runtime/loop.h"
#include "tiles/device.h"
#include "tiles/internal/capability.h"
#include "tiles/internal/graph_values.h"
#include "tiles/internal/placement.h"
#include "tiles/internal/regions.h"
#include <algorithm>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace camel::tiles {
namespace {
// Transfers are leaf operations. Preserve the source aggregate's structure in
// graph values instead of inventing a Device<tuple> or flattening field order.
runtime::gc_node_ref_t transferAggregate(
    runtime::GraphDraft &draft, runtime::gc_node_ref_t input, core::type::Type *host, bool upload,
    operator_t operation, const std::string &uri) {
    namespace rt   = runtime;
    namespace ty   = core::type;
    auto *resident = residentType(host);
    if (!resident)
        throw std::runtime_error("Tiles region transfer has an unsupported aggregate leaf");
    const bool tuple = host->code() == ty::TypeCode::Tuple;
    if (tuple || host->code() == ty::TypeCode::Struct) {
        auto *source       = upload ? host : resident;
        const size_t count = tuple ? static_cast<ty::TupleType *>(host)->size()
                                   : static_cast<ty::StructType *>(host)->size();
        std::vector<rt::gc_node_ref_t> fields;
        for (size_t i = 0; i < count; ++i) {
            auto *fieldType  = tuple ? static_cast<ty::TupleType *>(source)->typeAt(i)
                                     : static_cast<ty::StructType *>(source)->typeAt(i);
            auto *hostField  = tuple ? static_cast<ty::TupleType *>(host)->typeAt(i)
                                     : static_cast<ty::StructType *>(host)->typeAt(i);
            const auto field = tuple ? draft.addAccsNode(
                                           fieldType,
                                           static_cast<uint32_t>(i),
                                           draft.allocateRuntimeSlot(fieldType))
                                     : draft.addAccsNode(
                                           fieldType,
                                           static_cast<ty::StructType *>(host)->fieldName(i),
                                           draft.allocateRuntimeSlot(fieldType));
            draft.setNormInputs(field, std::span(&input, 1));
            fields.push_back(transferAggregate(draft, field, hostField, upload, operation, uri));
        }
        return packGraphAggregate(draft, upload ? resident : host, fields);
    }
    auto *type        = upload ? resident : host;
    const auto result = draft.addOperNode(type, operation, uri, draft.allocateRuntimeSlot(type));
    draft.setNormInputs(result, std::span(&input, 1));
    return result;
}
} // namespace

const runtime::Program *outlineRegions(
    core::context::Context &context, const runtime::Program *program, runtime::GCGraph *root,
    operator_t run, const std::string &uri, size_t &count, const RegionDeviceOps *device,
    bool refuseGpuScopes) {
    namespace rt = runtime;
    if (device &&
        (!device->upload || !device->execute || !device->download || run != device->execute))
        throw std::invalid_argument(
            "resident region rewriting requires matching transfer and execution operators");
    // Decorators can run on different Taskflow workers. A rewrite publishes detached
    // graphs into Context's graph registry, whose vectors are not concurrently mutable.
    // Serialize Tiles rewrites (not kernel execution) until that registry supports
    // concurrent publication. Acquire before suppressing collection for extraction.
    static std::mutex rewriteMutex;
    std::lock_guard rewriteLock(rewriteMutex);
    core::mm::NoCollectionScope noCollection;
    rt::RuntimeGraphDraftSession session(context.shared_from_this(), program, root);
    count             = 0;
    const auto graphs = session.collectReachableRuntimeGraphs();
    // A `#gpu` scope promises that the device runs everything written in it, and (GPU design 2.3,
    // 2.4) the functions it calls are part of that scope. Code written in an arm or a loop of a
    // covered graph is that region's segment, not a reason to fall back to the host (2.5), so the
    // scope map is built before the analysis and answers it.
    const GpuScopes scopes(*program);
    std::unordered_map<rt::GCGraph *, RegionAnalysis> analyses;
    for (auto *graph : graphs) {
        analyses.emplace(graph, analyzeRegions(*program, *graph, context.facts(), &scopes));
    }
    // Whether `id` is inside a `#gpu` scope: a device answer places it there directly, and a graph
    // the scope reaches places every node it did not explicitly tag for the host. A `#cpu` fact
    // is the host escape the design keeps, so it is never part of a device promise.
    const auto nodeInScope = [&](rt::GCGraph &graph, rt::gc_node_ref_t id,
                                 PlacementDecision &decision) {
        decision = placementOf(graph, id);
        return scopes.covers(graph, id);
    };
    const auto scopeNameOf = [&](rt::GCGraph &graph) {
        const auto *scope = scopes.scopeOf(graph);
        return std::string((scope == nullptr ? &graph : scope)->name());
    };
    const auto regionInScope = [&](rt::GCGraph &graph, const Region &region) {
        if (!refuseGpuScopes)
            return false;
        for (const auto member : region.members) {
            PlacementDecision decision;
            if (nodeInScope(graph, static_cast<rt::gc_node_ref_t>(member), decision))
                return true;
        }
        return false;
    };
    const auto reportRefusal = [&](rt::GCGraph &graph,
                                   rt::gc_node_ref_t id,
                                   core::error::RuntimeDiag code,
                                   const std::string &detail) {
        auto diag         = context.rtmDiags()->of(code);
        const auto origin = graph.nodeOrigin(id);
        if (origin != camel::source::kInvalidOriginId)
            diag.atOrigin(origin);
        const std::string name(
            core::OperatorTable::instance().uri(graph.nodeBodyAs<rt::GCOperBody>(id)->oper));
        // `GpuScopeNoDeviceProgram` writes the operator in quotes itself (「operator '{1}'」), so
        // it takes the bare name; `GpuScopeUnsupported` takes a reason phrase, where naming the
        // operator is part of the sentence.
        const bool bareName = code == core::error::RuntimeDiag::GpuScopeNoDeviceProgram;
        diag.commit(scopeNameOf(graph), bareName ? name : "operator '" + name + "'", detail);
    };
    if (refuseGpuScopes) {
        // Report every node the promise cannot keep and leave the graphs alone; the pass fails on
        // the diagnostics rather than running part of the program somewhere else.
        bool refused = false;
        for (auto *graph : graphs) {
            const auto &analysis = analyses.at(graph);
            for (rt::gc_node_ref_t id = 0; id < graph->nodeCount(); ++id) {
                if (graph->node(id)->kind != rt::GCNodeKind::Oper)
                    continue;
                PlacementDecision decision;
                if (!nodeInScope(*graph, id, decision))
                    continue;
                const auto oper = graph->nodeBodyAs<rt::GCOperBody>(id)->oper;
                if (!deviceCapable(oper)) {
                    // A device answer on the node itself is the operator having no device
                    // implementation; an untagged graph the scope reaches adds this operator to a
                    // device unit, and the module that provides it has no device program.
                    if (decision.onDevice())
                        reportRefusal(
                            *graph,
                            id,
                            core::error::RuntimeDiag::GpuScopeUnsupported,
                            "no device implementation");
                    else
                        reportRefusal(
                            *graph,
                            id,
                            core::error::RuntimeDiag::GpuScopeNoDeviceProgram,
                            "the module provides none");
                    refused = true;
                    continue;
                }
                // A lowering exists; the use may still have no program (a shape or element type
                // the back end refuses). An unresolved type or index instance is not a refusal:
                // the run specializes it from the actual arguments before it decides (h251).
                const char *reason = gpuRefusal(*graph, analysis, id);
                if (reason && *reason) {
                    reportRefusal(
                        *graph,
                        id,
                        core::error::RuntimeDiag::GpuScopeNoDeviceProgram,
                        reason);
                    refused = true;
                }
            }
        }
        if (refused)
            return program;
    }
    bool regionRefused = false;
    for (auto *graph : graphs) {
        const auto &analysis = analyses.at(graph);
        if (analysis.regions.empty())
            continue;
        auto &draft = session.edit(graph);
        std::unordered_map<size_t, rt::gc_node_ref_t> resident;
        const rt::ControlRegions control(*graph);
        // Where a segment runs: the control region of its members (plan.cn.md 2.5). An operator
        // written in an arm runs only when that arm is taken, so its call, its uploads and its
        // downloads are written there too; writing them in the body would run the untaken arm.
        const auto whereOf = [&](const Region &region) {
            return region.members.empty()
                       ? 0u
                       : control.controlOf(static_cast<rt::gc_node_ref_t>(region.members.front()));
        };
        // An upload is read by every region that takes that value as an input, so it is written in
        // the innermost control region that encloses all of them: two arms of one branch share one
        // upload in the body, because an arm may not read what another arm wrote.
        std::unordered_map<size_t, uint32_t> uploadControl;
        for (const auto &region : analysis.regions) {
            const uint32_t where = whereOf(region);
            for (auto value : region.inputs) {
                if (std::find(
                        analysis.transfers.uploads.begin(),
                        analysis.transfers.uploads.end(),
                        value) == analysis.transfers.uploads.end())
                    continue;
                const auto found = uploadControl.find(value);
                if (found == uploadControl.end()) {
                    uploadControl.emplace(value, where);
                    continue;
                }
                uint32_t common = found->second;
                while (common != 0 && !control.within(where, common))
                    common = control.parentControl(common);
                found->second = common;
            }
        }
        if (device) {
            for (auto value : analysis.transfers.uploads) {
                const auto input  = static_cast<rt::gc_node_ref_t>(value);
                const auto found  = uploadControl.find(value);
                draft.setInsertRegion(found == uploadControl.end() ? 0 : found->second);
                const auto upload = transferAggregate(
                    draft,
                    input,
                    graph->node(input)->dataType(),
                    true,
                    device->upload,
                    device->uploadUri);
                draft.setNodeOrigin(upload, graph->nodeOrigin(input));
                draft.setInsertRegion(0);
                resident.emplace(value, upload);
            }
        }
        std::vector<std::pair<rt::gc_node_ref_t, rt::gc_node_ref_t>> replacements;
        for (const auto &region : analysis.regions) {
            auto extracted  = extractRegion(*graph, region);
            const auto name = graph->name() + "/tiles.region/" + std::to_string(count++);
            extracted.graph->setId(context.graphIds().allocate());
            auto *callee = extracted.graph->encode(name, name);
            auto *fn     = ::Function::create(
                rt::makeProgram(context, program, std::span(&callee, 1)),
                callee->id(),
                core::type::TupleType::create(std::vector<core::type::Type *>{}),
                core::mm::autoSpace());
            if (run) {
                // Publish the owned compiled template as a graph fact. Draft commit
                // preserves this unedited callee's id and revision, including copies.
                try {
                    (void)compileTypes(fn, context.facts());
                } catch (const UnsupportedComputation &e) {
                    // A region is compiled from the static types its nodes carry; unlike a direct
                    // call there is no later run with actual shapes that could fix it. Inside a
                    // `#gpu` scope that is the refusal the design asks for; outside one the host
                    // may still run the region, which is what the pass did before.
                    if (!regionInScope(*graph, region))
                        continue;
                    auto diag =
                        context.rtmDiags()->of(core::error::RuntimeDiag::GpuScopeNoDeviceProgram);
                    const auto origin =
                        graph->nodeOrigin(static_cast<rt::gc_node_ref_t>(region.members.front()));
                    if (origin != camel::source::kInvalidOriginId)
                        diag.atOrigin(origin);
                    const bool shape =
                        e.oper().empty() || e.reason() == UnsupportedComputation::Reason::Shape;
                    diag.commit(
                        scopeNameOf(*graph),
                        // The message writes the operator in quotes itself; the name is passed bare.
                        e.oper().empty() ? std::string("<region>") : e.oper(),
                        shape ? "no device program for this shape"
                              : "no device program for this operator");
                    regionRefused = true;
                    break;
                }
            }
            if (regionRefused)
                break;
            auto fnNode =
                draft.materializeStaticValue(core::rtdata::toSlot(fn), callee->funcType());
            auto *type = callee->funcType()->exitType();
            std::vector<rt::gc_node_ref_t> args(
                extracted.parameters.begin(),
                extracted.parameters.end());
            if (device) {
                for (auto &arg : args)
                    arg = resident.at(arg);
                if (region.outputs.size() == 1)
                    type = residentType(type);
                else {
                    std::vector<core::type::Type *> fields;
                    for (auto value : region.outputs)
                        fields.push_back(residentType(
                            graph->node(static_cast<rt::gc_node_ref_t>(value))->dataType()));
                    type = core::type::TupleType::create(fields);
                }
            }
            rt::gc_node_ref_t call;
            // The call is written where its members were: in the region that holds them, so an
            // arm's segment runs in the arm and a loop's segment runs where the loop runs.
            draft.setInsertRegion(whereOf(region));
            if (run) {
                const auto packed = args.size() == 1 ? args.front() : packGraphTuple(draft, args);
                call              = draft.addOperNode(
                    type,
                    run,
                    device ? device->executeUri : uri,
                    draft.allocateRuntimeSlot(type));
                draft.setNormInputs(call, std::span(&packed, 1));
            } else {
                call = draft.addCallNode(type, {}, draft.allocateRuntimeSlot(type));
                draft.setNormInputs(call, args);
            }
            draft.setWithInputs(call, std::span(&fnNode, 1));
            // The members were statements of a chain (an operator written in an arm, a loop
            // written in the body). The call takes the place of the first of them that was the
            // head of that chain, so the segment keeps its order against the host statements
            // around it; erasing the members then closes the chain over it.
            for (auto member : region.members) {
                const auto ref   = static_cast<rt::gc_node_ref_t>(member);
                const auto preds = graph->ctrlInputsOf(ref);
                if (preds.empty())
                    continue;
                const bool head = std::none_of(
                    preds.begin(), preds.end(), [&](rt::gc_node_ref_t pred) {
                        return std::find(
                                   region.members.begin(), region.members.end(),
                                   static_cast<size_t>(pred)) != region.members.end();
                    });
                if (head) {
                    draft.takeStatementPlace(ref, call);
                    break;
                }
            }
            for (size_t i = 0; i < region.outputs.size(); ++i) {
                const auto old   = static_cast<rt::gc_node_ref_t>(region.outputs[i]);
                auto replacement = call;
                if (region.outputs.size() > 1) {
                    auto *fieldType = draft.header(old)->dataType;
                    if (device)
                        fieldType = residentType(fieldType);
                    replacement = draft.addAccsNode(
                        fieldType,
                        static_cast<uint32_t>(i),
                        draft.allocateRuntimeSlot(fieldType));
                    draft.setNormInputs(replacement, std::span(&call, 1));
                }
                draft.setNodeOrigin(replacement, graph->nodeOrigin(old));
                if (device) {
                    resident.emplace(old, replacement);
                    if (std::find(
                            analysis.transfers.downloads.begin(),
                            analysis.transfers.downloads.end(),
                            old) != analysis.transfers.downloads.end()) {
                        auto *hostType      = graph->node(old)->dataType();
                        const auto download = transferAggregate(
                            draft,
                            replacement,
                            hostType,
                            false,
                            device->download,
                            device->downloadUri);
                        draft.setNodeOrigin(download, graph->nodeOrigin(old));
                        replacement = download;
                    }
                }
                replacements.emplace_back(old, replacement);
            }
            draft.setInsertRegion(0);
        }
        // Host uses still refer to original IDs. Resident calls already use the
        // device map, so replacing host uses cannot route them through a download.
        for (auto [old, replacement] : replacements) {
            draft.replaceAllValueUses(old, replacement);
            if (draft.outputNode() == old)
                draft.setOutputNode(replacement);
            if (draft.exitNode() == old)
                draft.setExitNode(replacement);
            if (draft.returnNode() == old)
                draft.setReturnNode(replacement, draft.returnKind());
        }
        // The nodes of the condition and of the body of a loop went with the LOOP node.
        for (const auto &region : analysis.regions)
            for (auto id : region.members)
                if (graph->node(id)->kind == rt::GCNodeKind::Loop)
                    for (auto inner : rt::LoopView(*program, *graph, id).regionNodes())
                        draft.eraseNode(inner);
        for (const auto &region : analysis.regions)
            for (auto it = region.members.rbegin(); it != region.members.rend(); ++it)
                draft.eraseNode(static_cast<rt::gc_node_ref_t>(*it));
    }
    if (regionRefused)
        return program;
    return count ? session.commit() : program;
}
} // namespace camel::tiles
