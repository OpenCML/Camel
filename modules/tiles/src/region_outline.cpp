/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 18, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Extract eligible regions and replace them with typed calls, independent of device backend.
#include "camel/core/context/context.h"
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/runtime/draft_session.h"
#include "tiles/device.h"
#include "tiles/internal/capability.h"
#include "tiles/internal/graph_values.h"
#include "tiles/internal/regions.h"
#include <algorithm>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace camel::tiles {
runtime::GCGraph *outlineRegions(
    core::context::Context &context, runtime::GCGraph *root, operator_t run, const std::string &uri,
    size_t &count, const RegionDeviceOps *device) {
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
    rt::RuntimeGraphDraftSession session(context.shared_from_this(), root);
    count = 0;
    for (auto *graph : session.collectReachableRuntimeGraphs()) {
        const auto analysis = analyzeRegions(*graph, context.facts());
        if (analysis.regions.empty())
            continue;
        auto &draft = session.edit(graph);
        std::unordered_map<size_t, rt::gc_node_ref_t> resident;
        if (device) {
            for (auto value : analysis.transfers.uploads) {
                const auto input = static_cast<rt::gc_node_ref_t>(value);
                auto *type       = DeviceType::get(graph->node(input)->dataType());
                auto upload      = draft.addOperNode(
                    type,
                    device->upload,
                    device->uploadUri,
                    draft.allocateRuntimeSlot(type));
                draft.setNormInputs(upload, std::span(&input, 1));
                draft.setNodeOrigin(upload, graph->nodeOrigin(input));
                resident.emplace(value, upload);
            }
        }
        std::vector<std::pair<rt::gc_node_ref_t, rt::gc_node_ref_t>> replacements;
        for (const auto &region : analysis.regions) {
            auto extracted  = extractRegion(*graph, region);
            const auto name = graph->stableId() + "/tiles.region/" + std::to_string(count++);
            auto *callee    = extracted.graph->encode(name, name, name);
            auto *fn        = ::Function::create(
                callee,
                core::type::TupleType::create(std::vector<core::type::Type *>{}),
                core::mm::autoSpace());
            if (run) {
                // Publish the owned compiled template as a graph fact. Draft commit
                // preserves this unedited callee's lineage/revision, including copies.
                try {
                    (void)compileTypes(fn, context.facts());
                } catch (const UnsupportedComputation &) {
                }
            }
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
                    type = DeviceType::get(type);
                else {
                    std::vector<core::type::Type *> fields;
                    for (auto value : region.outputs)
                        fields.push_back(
                            DeviceType::get(
                                graph->node(static_cast<rt::gc_node_ref_t>(value))->dataType()));
                    type = core::type::TupleType::create(fields);
                }
            }
            rt::gc_node_ref_t call;
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
            for (size_t i = 0; i < region.outputs.size(); ++i) {
                const auto old   = static_cast<rt::gc_node_ref_t>(region.outputs[i]);
                auto replacement = call;
                if (region.outputs.size() > 1) {
                    auto *fieldType = draft.header(old)->dataType;
                    if (device)
                        fieldType = DeviceType::get(fieldType);
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
                        auto *hostType = graph->node(old)->dataType();
                        auto download  = draft.addOperNode(
                            hostType,
                            device->download,
                            device->downloadUri,
                            draft.allocateRuntimeSlot(hostType));
                        draft.setNormInputs(download, std::span(&replacement, 1));
                        draft.setNodeOrigin(download, graph->nodeOrigin(old));
                        replacement = download;
                    }
                }
                replacements.emplace_back(old, replacement);
            }
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
        for (const auto &region : analysis.regions)
            for (auto it = region.members.rbegin(); it != region.members.rend(); ++it)
                draft.eraseNode(static_cast<rt::gc_node_ref_t>(*it));
    }
    return count ? session.commit() : root;
}
} // namespace camel::tiles
