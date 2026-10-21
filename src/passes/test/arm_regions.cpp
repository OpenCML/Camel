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
 * Created: Oct. 22, 2026
 * Updated: Oct. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Checks that the Arm region of every arm of every branch holds the nodes the arm reckoning of the
 * back ends finds (verifyBranchArmRegions), and prints for the test plan:
 *   ARM_REGIONS graphs=2 branches=3 arm_regions=6
 * The back ends do not read the regions yet; the program runs on as before.
 */
#include "camel/passes/test/arm_regions.h"

#include "camel/execute/graph_runtime_support.h"

const camel::runtime::Program *
ArmRegionsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    using namespace camel::runtime;
    size_t count = 0, branches = 0, armRegions = 0;
    for (GCGraph *graph : program->graphs()) {
        ++count;
        const auto topo = camel::execute::computeExecutionOrder(graph);
        camel::execute::verifyBranchArmRegions(graph, topo);
        for (gc_node_ref_t id : topo) {
            branches += graph->node(id)->kind == GCNodeKind::Brch;
        }
        for (uint32_t r = 1; r < graph->regionCount(); ++r) {
            armRegions += graph->region(r)->kind == GCRegionKind::Arm;
        }
    }
    os << "ARM_REGIONS graphs=" << count << " branches=" << branches
       << " arm_regions=" << armRegions << "\n";
    return program;
}
