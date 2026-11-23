// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: one graph, two readings of the same node's input ports.
 *
 * The face (`kernel/graph/graph_view.h`) answers a node's ports through `inputs` and
 * `predecessors`, and every reader that moved onto the face reads them that way. What this pass
 * pins is the claim that migration rests on: for a node the frozen graph holds, the ports the
 * graph itself reports and the ports the face reports are the same list, in the same order -- no
 * extra, no missing, node by node, over every graph of the program.
 *
 * The nodes are enumerated from the **graph** (`ref < nodeCount()`, a header at that ref) and never
 * from `view.nodes()`. A ref the view does not yield would make the comparison one empty set
 * against another, and the reading would be green in exactly the case it exists to catch: the face
 * refuses a JOIN (and a DREF, and a dead place), so a walk over `view.nodes()` never reaches the
 * node whose ports are in question.
 *
 * For the same reason the pass counts the JOINs it compared and prints them whether they differ or
 * not: a program whose branch was folded away, or whose branch joins no value, has none, and a
 * comparison of two empty answers would be agreement about nothing. A plan pins `[VP] join ` so
 * that such a program cannot pass as a green reading -- that is how this case was red on its first
 * run (`joins=0` on a graph that keeps a BRCH with two arms but no JOIN).
 *
 *   [VP] graph=G join ref=R graph=N view=M
 *        one JOIN of graph G: how many ports each side answers for it. Printed always.
 *   [VP] agree=1 graphs=N nodes=N ports=N joins=N joinports=N
 *        every node of every graph answers the same list on both sides, with the counts printed so
 *        a run that compared nothing cannot pass as agreement.
 *   [VP] graph=G ref=R graph=N view=M missing=norm#0,with#0,with#1
 *        one node that differed: how many ports each side answers and which ones the face does not,
 *        named by the group they sit in. A JOIN's list is `norm#0` (the BRCH it closes) and one
 *        `with#` per arm -- the values the arms hand out, which is what a cut has to carry.
 */

#include "camel/passes/test/view_ports.h"

#include "camel/execute/pass/registration.h"
#include "graph/callee_graph_of.h"
#include "graph/gc_graph_view.h"

#include <string>
#include <vector>

using camel::graph::EdgeKind;
using camel::graph::NodeRef;

namespace {

/// The ports the graph itself reports for `ref`, in the order `predecessors(Data)` answers in: the
/// ordinary group first and then the context one.
std::vector<NodeRef>
graphPortsOf(const camel::runtime::GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    std::vector<NodeRef> ports;
    for (const camel::runtime::gc_node_ref_t in : graph.normInputsOf(ref)) {
        ports.push_back(in);
    }
    for (const camel::runtime::gc_node_ref_t in : graph.withInputsOf(ref)) {
        ports.push_back(in);
    }
    return ports;
}

} // namespace

const camel::runtime::Program *
ViewPortsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr || program->graphs().empty()) {
        return program;
    }
    const camel::graph::CalleeGraphOf calleeGraphOf =
        [program](camel::runtime::GraphId id) -> const camel::graph::Graph * {
        return program->graph(id);
    };

    size_t graphs    = 0;
    size_t nodes     = 0;
    size_t ports     = 0;
    size_t differing = 0;
    size_t joins     = 0;
    size_t joinPorts = 0;
    // `graphs()` builds the list on each call, so it is held once: a temporary's address is not a
    // list of graphs.
    const auto all = program->graphs();
    for (size_t g = 0; g < all.size(); ++g) {
        const camel::runtime::GCGraph *target = all[g];
        if (target == nullptr || target->nodeCount() < 4) {
            continue;
        }
        ++graphs;
        const camel::graph::FrozenGraphAdapter view(*target, calleeGraphOf);
        for (camel::runtime::gc_node_ref_t ref = 0; ref < target->nodeCount(); ++ref) {
            if (target->node(ref) == nullptr) {
                continue;
            }
            ++nodes;
            const std::vector<NodeRef> graphPorts = graphPortsOf(*target, ref);
            const std::vector<NodeRef> viewPorts  = view.predecessors(ref, EdgeKind::Data);
            ports += graphPorts.size();
            if (target->node(ref)->kind == camel::runtime::GCNodeKind::Join) {
                ++joins;
                joinPorts += graphPorts.size();
                os << "[VP] graph=" << g << " join ref=" << ref << " graph=" << graphPorts.size()
                   << " view=" << viewPorts.size() << "\n";
            }
            if (graphPorts == viewPorts) {
                continue;
            }
            ++differing;
            // Which ports the face does not answer. The two lists share their order, so a port the
            // face answers is at the same index on both sides and the difference is the tail;
            // naming them is what tells a reader whether the missing ones are the ordinary group or
            // the arm values.
            const size_t normCount = target->normInputsOf(ref).size();
            std::string missing;
            for (size_t i = viewPorts.size(); i < graphPorts.size(); ++i) {
                if (!missing.empty()) {
                    missing += ",";
                }
                missing += i < normCount ? "norm#" + std::to_string(i)
                                         : "with#" + std::to_string(i - normCount);
            }
            os << "[VP] graph=" << g << " ref=" << ref << " graph=" << graphPorts.size()
               << " view=" << viewPorts.size()
               << " missing=" << (missing.empty() ? "order" : missing) << "\n";
        }
    }
    os << "[VP] agree=" << (differing == 0 ? 1 : 0) << " graphs=" << graphs << " nodes=" << nodes
       << " ports=" << ports << " joins=" << joins << " joinports=" << joinPorts << "\n";
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements, under the name its header declares.
const PassKernel viewPortsKernel(kViewPortsTestPass, makePass<ViewPortsTestPass>());

} // namespace