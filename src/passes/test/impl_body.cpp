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
 * TEST-ONLY: prints the shape of every graph of the program, the semantic bodies of the
 * `#impl` functions included (they are usually not reachable from the root, so `std::gir` does
 * not show them). `IMPL body <name>` marks a graph Program::implBodies returns; then one
 * `IMPL node` line per node, label and inputs, so two graphs can be compared node by node.
 */

#include "impl_body.h"

#include "camel/core/node_attrs.h"
#include "camel/passes/trans/gir_metadata.h"
#include "camel/runtime/graph.h"

#include <algorithm>
#include <format>
#include <span>
#include <vector>

namespace {

std::string inputList(const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    std::string inputs;
    const auto append = [&](std::span<const camel::runtime::gc_node_ref_t> refs) {
        for (const camel::runtime::gc_node_ref_t input : refs) {
            inputs += inputs.empty() ? "" : ",";
            inputs += std::to_string(input);
        }
    };
    append(graph.normInputsOf(ref));
    append(graph.withInputsOf(ref));
    append(graph.ctrlInputsOf(ref));
    return inputs;
}

} // namespace

const camel::runtime::Program *
ImplBodyTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    std::vector<camel::runtime::GCGraph *> graphs = program->graphs();
    std::sort(graphs.begin(), graphs.end(), [](const auto *a, const auto *b) {
        return a->name() < b->name();
    });
    os << std::format("IMPL bodies {}\n", program->implBodies().size());
    for (camel::runtime::GCGraph *graph : graphs) {
        os << std::format(
            "IMPL graph {} body={} nodes={} edges={}\n",
            graph->name(),
            graph->isImplBody() ? "yes" : "no",
            graph->liveNodeCount(),
            graph->edgeCount());
        if (graph->regionCount() > 0 &&
            graph->region(0)->attrs != camel::core::attr::kNoAttrs) {
            os << std::format(
                "IMPL attrs {} {}\n",
                graph->name(),
                camel::core::attr::text(graph->region(0)->attrs));
        }
        for (auto it = graph->nodes().begin(); it != graph->nodes().end(); ++it) {
            const camel::runtime::GCNode &node = **it;
            os << std::format(
                "IMPL node {} {} {} {} in={}\n",
                graph->name(),
                it.ref(),
                camel::passes::gir::recordType(node),
                camel::passes::gir::recordLabel(*program, graph, it.ref(), node),
                inputList(*graph, it.ref()));
        }
    }
    return program;
}