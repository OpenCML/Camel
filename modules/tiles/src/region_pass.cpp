// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Public region compilation pass. Device selection happens at execution time.

#include "camel/core/tags.h"
#include "camel/execute/pass/opt.h"
#include "tiles/internal/placement.h"
#include "tiles/internal/regions.h"
#include "tiles/internal/residency.h"
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
        // A `#gpu` scope's gradient is not expanded here: `grad` and `with_grad` are macro
        // operators, and the pass that builds their derived graph ahead of time is `std::macro`
        // (design principles 8), which the pipeline writes before this one. This pass is a
        // consumer of that work, never its author: a scope that still holds an unexpanded macro
        // operator is refused and the message names `std::macro`
        // (docs/design/gpu/plan.cn.md 2.6).
        //
        // The placement decision this step pins: the report is the query's answer for every
        // graph, printed before any rewrite of this pass. It does not choose an executor; it
        // makes the answer observable, and a later step is where refusal and selection act on
        // it. A derived graph the expansion published is a graph like any other here, and the
        // report shows the placement it inherited from its primal (plan 2.3).
        out << placementReport(*program);
        // The residency conclusion of the same program (plan 3.2): which block each value lives
        // in, which values change class at a boundary, and what a call between two graphs of one
        // class moves (nothing: a parameter follows its argument's block). Printed beside the
        // placement report, before the rewrite, and empty for a program that writes no fact, so
        // an untagged program's output is unchanged. It reads the graph; it inserts no node yet.
        out << residencyReport(*program);
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
