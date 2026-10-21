/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Camel Contributors
 * Created: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Moves the body of a loop that is in a branch arm out of the arm (a second LoopBody region in
 * the body of the graph) and commits, then prints whether the commit accepted it:
 *   ARM_LOOP_REGIONS refused: Runtime graph 'maybe' breaks the branch-arm invariant: LOOP 8 is in
 *   arm 0 of BRCH 3 (region 1) but its body region 4 is a region of the body (region 0): ...
 */

#include "camel/passes/test/arm_loop_regions.h"

#include "camel/runtime/control_regions.h"
#include "camel/runtime/draft_session.h"

#include <exception>

namespace {

using namespace camel::runtime;

/// The first live LOOP that runs in a branch arm, with the body region it has there.
struct ArmLoop {
    GCGraph *graph         = nullptr;
    gc_node_ref_t loop     = kInvalidNodeRef;
    uint32_t bodyRegion    = 0;
};

ArmLoop findArmLoop(const Program *program) {
    for (GCGraph *graph : program->graphs()) {
        if (!graph->hasNodePayload()) {
            continue;
        }
        const ControlRegions regions(*graph);
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            if (!*it || (*it)->kind != GCNodeKind::Loop) {
                continue;
            }
            const auto parts = regions.loopPartsOf(it.ref());
            if (parts[1] == 0 || regions.controlOf(it.ref()) == 0) {
                continue;
            }
            return ArmLoop{graph, it.ref(), parts[1]};
        }
    }
    return {};
}

} // namespace

const camel::runtime::Program *
ArmLoopRegionsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    const ArmLoop found = findArmLoop(program);
    if (found.graph == nullptr) {
        os << "ARM_LOOP_REGIONS none\n";
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    GraphDraft &draft = session.edit(found.graph);
    // The same loop, with a body region in the body of the graph instead of in the arm it runs in.
    draft.addRegion(GCRegionKind::LoopBody, 0, found.loop, 0);
    try {
        const Program *next = session.commit();
        os << "ARM_LOOP_REGIONS accepted\n";
        return next;
    } catch (const std::exception &error) {
        os << "ARM_LOOP_REGIONS refused: " << error.what() << "\n";
        return program;
    }
}