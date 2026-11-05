// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

// Public region compilation pass. Device selection happens at execution time.

#include "camel/core/tags.h"
#include "camel/execute/pass/opt.h"
#include "camel/passes/opt/generic.h"
#include "tiles/internal/placement.h"
#include "tiles/internal/regions.h"
#include "tiles/internal/residency.h"
#include <memory>
#include <utility>

namespace camel::tiles {
namespace {
class RegionPass final : public RuntimeGraphRewritePass {
    SegmentDeviceOps preferred_;

  public:
    RegionPass(const core::context::context_ptr_t &context, SegmentDeviceOps preferred)
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
        // What the segmentation decides depends on which operators are still in the graph, and one
        // family of them is not an operator at all: a fact of an argument's type (`shape`, `numel`,
        // `dim`) is a constant the type folder answers, and the device path has no program for it
        // because there is nothing to compute.
        //
        // `std::opt::fold` folds those, and a pipeline that names it before this pass leaves nothing
        // for this step to do. The pass cannot require that pass (its contract is to consume the
        // program it is handed, and a pipeline may reasonably not optimize), so it runs the same
        // fold -- `foldTypedOperators` is the very function `std::opt::fold` reaches, called once
        // here. It runs before the cut and not inside a segment: a segment that folded for itself
        // would fold the same node once per segment, and every repetition is an operator call and
        // an allocation the report of the run would show. The reports above are printed first
        // because they are the reading of the program this pass was handed.
        program = foldTypedOperators(context_, program);
        size_t count    = 0;
        slot_t refusal  = NullSlot;
        const auto *result = outlineRegions(
            *context_,
            program,
            program->root(),
            preferred_.execute,
            preferred_.executeUri,
            count,
            &preferred_,
            /*refuseGpuScopes=*/true,
            &refusal);
        if (refusal != NullSlot) {
            // A `#gpu` scope holds an operator the device cannot run: the pass cannot keep the
            // scope's promise, so it fails, and its failure is an error value (design principles
            // 9.1). The driver stops the pipeline at this pass and reports it the way an error
            // value nobody handled is (3.9). The diagnostic is the other refusal, the missing
            // `std::macro`: the graphs are left unrewritten in both cases.
            failWithErrorValue(refusal);
            return nullptr;
        }
        return result;
    }
};
} // namespace
void registerRegionPass(SegmentDeviceOps preferred) {
    registerModulePass("tiles::compile", [preferred](const core::context::context_ptr_t &context) {
        return std::make_unique<RegionPass>(context, preferred);
    });
    // The pass reads the kernel, tile and permission facts of the nodes it compiles.
    core::tag::declareReader("tiles::compile");
}
} // namespace camel::tiles
