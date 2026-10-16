/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license: https://opensource.org/license/mit
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND.
 * Author: Camel Contributors
 * Created: Oct. 16, 2026
 * Updated: Oct. 16, 2026
 * Supported by: National Key Research and Development Program of China
 */

// Test outlining by executing the extracted functions on ordinary CPU backends.
#include "camel/core/mm/gen.h"
#include "camel/core/rtdata/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/execute/pass/opt.h"
#include "camel/runtime/draft_session.h"
#include "tiles/internal/regions.h"
#include <cstdlib>
#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>
namespace camel::tiles {
namespace {
class RegionOutlineTestPass final : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    runtime::GCGraph *apply(runtime::GCGraph *root, std::ostream &os) override {
        namespace rt = runtime;
        core::mm::NoCollectionScope noCollection;
        rt::RuntimeGraphDraftSession session(context_, root);
        size_t count = 0;
        for (auto *graph : session.collectReachableRuntimeGraphs()) {
            const auto analysis = analyzeRegions(*graph, context_->facts());
            if (analysis.regions.empty())
                continue;
            auto &draft = session.edit(graph);
            std::vector<std::pair<rt::gc_node_ref_t, rt::gc_node_ref_t>> replacements;
            for (const auto &region : analysis.regions) {
                auto extracted  = extractRegion(*graph, region);
                const auto name = graph->stableId() + "/tiles.region/" + std::to_string(count++);
                auto *callee    = extracted.graph->encode(name, name, name);
                auto *fn        = ::Function::create(
                    callee,
                    core::type::TupleType::create(std::vector<core::type::Type *>{}),
                    core::mm::autoSpace());
                auto fnNode =
                    draft.materializeStaticValue(core::rtdata::toSlot(fn), callee->funcType());
                auto *type = callee->funcType()->exitType();
                auto call  = draft.addCallNode(type, {}, draft.allocateRuntimeSlot(type));
                draft.setWithInputs(call, std::span(&fnNode, 1));
                std::vector<rt::gc_node_ref_t> args(
                    extracted.parameters.begin(),
                    extracted.parameters.end());
                draft.setNormInputs(call, args);
                for (size_t i = 0; i < region.outputs.size(); ++i) {
                    const auto old   = static_cast<rt::gc_node_ref_t>(region.outputs[i]);
                    auto replacement = call;
                    if (region.outputs.size() > 1) {
                        auto *fieldType = draft.header(old)->dataType;
                        replacement     = draft.addAccsNode(
                            fieldType,
                            static_cast<uint32_t>(i),
                            draft.allocateRuntimeSlot(fieldType));
                        draft.setNormInputs(replacement, std::span(&call, 1));
                    }
                    draft.setNodeOrigin(replacement, graph->nodeOrigin(old));
                    replacements.emplace_back(old, replacement);
                }
            }
            // Build all calls first: their inputs still use original IDs, including outputs
            // of other regions. Replacing uses then rewires every inter-region dependency.
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
        os << "TILES_OUTLINED " << count << '\n';
        return count ? session.commit() : root;
    }
};
} // namespace
void registerRegionTestPasses() {
    if (std::getenv("CAMEL_ENABLE_TEST_PASSES"))
        registerModulePass("tiles::test_outline", [](const core::context::context_ptr_t &ctx) {
            return std::make_unique<RegionOutlineTestPass>(ctx);
        });
}
} // namespace camel::tiles
