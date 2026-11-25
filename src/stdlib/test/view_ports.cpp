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
 *
 *   [VW] agree=1 attrs=N perms=N wait=N users=N edges=E regions=R
 *        the other readings the face answers, compared against the graph's own on the same walk: a
 *        node's tag set (`nodeAttrs`), its permission column (`nodePermissions`), the wait in both
 *        directions (`waitInputsOf` / `waitUsersOf`), the whole sync and order tables
 *        (`syncEdges` / `orderEdges`, as (pred, user) pairs) and every region's tag set
 *        (`GCRegion::attrs`). The counts are per node, or per graph for `edges`/`regions`, so a
 *        comparison that reached nothing cannot pass as agreement.
 */

#include "camel/passes/test/view_ports.h"

#include "camel/execute/pass/registration.h"
#include "graph/callee_graph_of.h"
#include "graph/gc_graph_view.h"

#include <algorithm>
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
    // The columns and the edges the face answers beside the ports (a lane added them: the tag set,
    // the permission column, the wait and the two side tables). Compared the same way -- against
    // the graph's own answer, node by node, region by region -- and counted so that a program the
    // comparison never reached cannot pass as agreement.
    size_t attrsCompared  = 0;
    size_t permsCompared  = 0;
    size_t waitsCompared  = 0;
    size_t succsCompared  = 0;
    size_t edgesCompared  = 0;
    size_t regionsCompared = 0;
    size_t columnsDiffer  = 0;
    const auto note = [&](bool same, size_t &count) {
        ++count;
        if (!same) {
            ++columnsDiffer;
        }
    };
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
        // The regions first: their records carry the tag set a region's ports need, and region 0 is the
        // body even when the graph's own table is empty (`region(0)` answers nullptr then, and the
        // body's facts are none).
        for (camel::runtime::gc_cnt_t id = 0; id < target->regionCount(); ++id) {
            const camel::runtime::GCRegion *stored = target->region(id);
            const camel::graph::RegionRecord answered = view.region(id);
            note(
                answered.attrs ==
                    (stored == nullptr ? camel::core::attr::kNoAttrs : stored->attrs),
                regionsCompared);
        }
        // The two tables the graph keeps as side arrays, as (pred, user) pairs in the same order.
        {
            std::vector<camel::graph::Edge> expected;
            for (const camel::runtime::GCGraphSyncEdge &edge : target->syncEdges()) {
                expected.push_back({edge.pred, edge.user});
            }
            const std::span<const camel::graph::Edge> answered = view.edges(EdgeKind::Sync);
            note(
                answered.size() == expected.size() &&
                    std::equal(answered.begin(), answered.end(), expected.begin()),
                edgesCompared);
            expected.clear();
            for (const camel::runtime::GCGraphOrderEdge &edge : target->orderEdges()) {
                expected.push_back({edge.pred, edge.user});
            }
            const std::span<const camel::graph::Edge> answeredOrders = view.edges(EdgeKind::Order);
            note(
                answeredOrders.size() == expected.size() &&
                    std::equal(answeredOrders.begin(), answeredOrders.end(), expected.begin()),
                edgesCompared);
        }
        for (camel::runtime::gc_node_ref_t ref = 0; ref < target->nodeCount(); ++ref) {
            if (target->node(ref) == nullptr) {
                continue;
            }
            ++nodes;
            const std::vector<NodeRef> graphPorts = graphPortsOf(*target, ref);
            const std::vector<NodeRef> viewPorts  = view.predecessors(ref, EdgeKind::Data);
            ports += graphPorts.size();
            // The two columns a node carries, and the wait in both directions: the same reading the
            // graph answers, so a reader that moved off `nodeAttrs`/`nodePermissions`/
            // `waitInputsOf`/`waitUsersOf` and onto the face reads what it read before.
            note(
                view.attrsOf(ref) ==
                    static_cast<camel::core::attr::AttrSetId>(target->nodeAttrs(ref)),
                attrsCompared);
            note(view.permissionsOf(ref) == target->nodePermissions(ref), permsCompared);
            note(view.predecessors(ref, EdgeKind::Wait) == target->waitInputsOf(ref), waitsCompared);
            note(view.successors(ref, EdgeKind::Wait) == target->waitUsersOf(ref), succsCompared);
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
    // The columns, the wait and the two side tables, with the counts printed for the same reason:
    // `regions` and `edges` are per graph and the rest are per node, and a comparison that reached
    // nothing must not read as agreement.
    os << "[VW] agree=" << (columnsDiffer == 0 ? 1 : 0) << " attrs=" << attrsCompared
       << " perms=" << permsCompared << " wait=" << waitsCompared << " users=" << succsCompared
       << " edges=" << edgesCompared << " regions=" << regionsCompared << "\n";
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only passes this file implements, under the name its header declares.
const PassKernel viewPortsKernel(kViewPortsTestPass, makePass<ViewPortsTestPass>());

} // namespace