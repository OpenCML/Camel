// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Public region compilation pass. Device selection happens at execution time.

#include "camel/core/tags.h"
#include "camel/execute/pass/opt.h"
#include "tiles/internal/placement.h"
#include "tiles/internal/regions.h"
#include <memory>
#include <utility>

namespace camel::tiles {
namespace {
class RegionPass final : public RuntimeGraphRewritePass {
    RegionDeviceOps preferred_;

  public:
    RegionPass(const core::context::context_ptr_t &context, RegionDeviceOps preferred)
        : RuntimeGraphRewritePass(context), preferred_(std::move(preferred)) {}
    const runtime::Program *apply(const runtime::Program *program, std::ostream &out) override {
        // The placement decision this step pins: the report is the query's answer for every
        // graph, printed before any rewrite. It does not choose an executor; it makes the
        // answer observable, and a later step is where refusal and selection act on it.
        out << placementReport(*program);
        size_t count = 0;
        return outlineRegions(
            *context_,
            program,
            program->root(),
            preferred_.execute,
            preferred_.executeUri,
            count,
            &preferred_,
            /*refuseGpuScopes=*/true);
    }
};
} // namespace
void registerRegionPass(RegionDeviceOps preferred) {
    registerModulePass("tiles::compile", [preferred](const core::context::context_ptr_t &context) {
        return std::make_unique<RegionPass>(context, preferred);
    });
    // The pass reads the kernel, tile and permission facts of the nodes it compiles.
    core::tag::declareReader("tiles::compile");
}
} // namespace camel::tiles
