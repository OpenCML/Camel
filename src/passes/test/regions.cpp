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
 * Updated: Oct. 21, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Puts nodes of the largest graph of the program in two nested regions, commits, decodes the new
 * version and commits again, and prints what it finds for the test plan:
 *   REGIONS count=3 in_inner=2 in_outer=1 result=ok ports=1 within=1
 *   REGIONS recommit same=1
 * The regions mean nothing to the back ends yet; the program runs on as before.
 */
#include "camel/passes/test/regions.h"

#include "camel/runtime/draft_session.h"

const camel::runtime::Program *
RegionsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    GCGraph *largest = nullptr;
    for (GCGraph *graph : program->graphs()) {
        if (largest == nullptr || graph->nodeCount() > largest->nodeCount()) {
            largest = graph;
        }
    }
    if (largest == nullptr || largest->liveNodeCount() < 8) {
        return program;
    }
    RuntimeGraphDraftSession session(context_, program);
    GraphDraft &draft = session.edit(largest);
    std::vector<gc_node_ref_t> live;
    for (gc_node_ref_t id = 0; id < draft.nodeSlotCount() && live.size() < 6; ++id) {
        if (draft.alive(id)) {
            live.push_back(id);
        }
    }
    const uint32_t outer = draft.addRegion(GCRegionKind::Block, 0);
    const uint32_t inner = draft.addRegion(GCRegionKind::Block, outer);
    draft.setNodeRegion(live[2], outer);
    draft.setNodeRegion(live[3], inner);
    draft.setNodeRegion(live[4], inner);
    draft.setRegionResult(inner, live[4]);
    draft.setRegionPorts(outer, std::span<const gc_node_ref_t>(&live[1], 1));
    const Program *next = session.commit();

    const GCGraph *after = next->graph(largest->id());
    size_t inInner = 0, inOuter = 0;
    for (gc_node_ref_t id = 0; after && id < after->nodeCount(); ++id) {
        inInner += after->nodeRegion(id) == inner;
        inOuter += after->nodeRegion(id) == outer;
    }
    const bool resultOk = after && after->region(inner) && after->region(inner)->result == live[4];
    const bool portsOk  = after && after->regionPorts(outer).size() == 1 &&
                         after->regionPorts(outer)[0] == live[1];
    os << "REGIONS count=" << (after ? after->regionCount() : 0) << " in_inner=" << inInner
       << " in_outer=" << inOuter << " result=" << (resultOk ? "ok" : "bad")
       << " ports=" << (portsOk ? 1 : 0) << " within=" << (inInner == 2 ? 1 : 0) << "\n";

    // A decode and an encode with no change in between give the same version.
    RuntimeGraphDraftSession again(context_, next);
    again.edit(const_cast<GCGraph *>(after));
    const Program *last = again.commit();
    os << "REGIONS recommit same=" << (last->graph(largest->id()) == after ? 1 : 0) << "\n";
    return next;
}
