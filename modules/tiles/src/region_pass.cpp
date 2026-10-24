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
        // `tiles::compile` applies `autodiff::expand` to the scope graphs first
        // (docs/design/gpu/plan-modules.cn.md 5, decision 2: a `#gpu` scope's expansion is
        // triggered here, so the pipeline's spelling does not change and a user never writes the
        // pass). Without it a `grad`/`with_grad` call inside a scope keeps its derived graph in
        // the run, where no pass — this one included — ever sees it
        // (docs/design/gpu/plan.cn.md 2.6). The pass is idempotent, and it changes nothing
        // outside a `#gpu` scope, so the graphs of a program without the tag are byte-identical
        // with and without it.
        const auto expanded = applyRegisteredPass(program, "autodiff::expand", context_, out);
        if (expanded.program == nullptr) {
            // The expansion refused a scope (plan 2.6.3): its diagnostic is committed, and the
            // program is left as it was rather than half-expanded.
            return program;
        }
        program = expanded.program;
        // The placement decision this step pins: the report is the query's answer for every
        // graph, printed before any rewrite of this pass. It does not choose an executor; it
        // makes the answer observable, and a later step is where refusal and selection act on
        // it. A derived graph the expansion just published is a graph like any other here, and
        // the report shows the placement it inherited from its primal (plan 2.3).
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
