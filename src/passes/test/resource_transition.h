// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * TEST-ONLY: `test::resource_transition` asks the two copies of the effect-masking rule the same
 * question about one program (docs/design/effects/plan.cn.md step 5; design principles 3.3). The
 * builder's walk (compile/gir/builder_resources.cpp#placeCapturedUse) and the runtime's walk
 * (runtime/resources.cpp#FootprintWalk) both drop the accesses a body makes to a cell it made
 * itself; R3 replaces them with one analysis, and until then this pass is what keeps them from
 * drifting apart unseen. One line per graph: `MASK <graph> builder=<...> runtime=<...>
 * agree=yes|no`.
 *
 *   - `builder` is the reach the builder measured for the body of that graph, read back through
 *     compile/gir/resource_transition.h. It is the builder's own walk, including the sets it cannot
 *     write onto a type -- and so cannot be read back from the frozen graph's function type alone;
 *   - `runtime` is `runtime::footprintOf` over the frozen graph: the runtime's own walk.
 *
 * Both are written in the runtime's footprint text (src/runtime/resources.cpp#toString), because
 * the two answers are one claim in two notations and only the notation is shared.
 *
 * Two parts of a footprint are outside the comparison, and the plan says why: `result` (the
 * builder's measurement answers what the body reaches and records the conservative `Unknown` there,
 * while the runtime answers what the result aliases from the shapes the graph has) and `allocates`
 * (a GraphFootprint carries none). Everything else -- the resources, their classes, the binding
 * that names them (a parameter, a capture, an ambient name), their access mode and the external
 * effect -- is compared.
 *
 * The program is returned unchanged.
 */

#pragma once

#include "camel/execute/pass/opt.h"

inline constexpr const char *kResourceTransitionTestPass = "test::resource_transition";

class ResourceTransitionTestPass : public RuntimeGraphRewritePass {
  public:
    using RuntimeGraphRewritePass::RuntimeGraphRewritePass;
    const camel::runtime::Program *
    apply(const camel::runtime::Program *program, std::ostream &os) override;
};