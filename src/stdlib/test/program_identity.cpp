// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/* Test-only: a Program answers about a graph by the graph's identity.
 *
 * The two entries that used to be asked with a representation -- `Program::calleeOf` and
 * `Context::programOfRuntimeGraph` -- are asked here with the graph as a value (`const
 * camel::graph::Graph *`, the handle `CalleeRef::graph` and a fact's anchor already name), and the
 * reading is that the answer depends on **which graph** was named and on nothing else:
 *
 *   - the graph a call reaches is answered for the graph the reader is standing on, so a FUNC node
 *     of graph A never answers graph B's callee, and a ref that is not a call in a graph answers
 *     none there;
 *   - a value the program does not hold answers "no graph" and "no program" rather than being read
 *     as one of the program's own graphs: this pass hands over a draft of a graph -- the other
 *     representation of the same seam (`GraphDraft : graph::Graph`) -- and the answer must be none;
 *   - the same graph reached two ways answers once: the program's identity lookup
 *     (`Program::calleeOf`) and the view's record lookup with the program as the resolver
 *     (`GraphView::calleeOf`) name the same graph value;
 *   - two places in the source that name one graph answer with one value: the call sites are
 *     grouped by the value they answered, and the largest group is printed (`shared`). An answer
 *     keyed on a node or on a text cannot group two positions this way -- it has one answer per
 *     position, or one per spelling;
 *   - and one spelling with two identities is not one graph: the draft read below has the same
 *     nodes and the same types as the graph it is a draft of (`test::view_types` reads both sides)
 *     and is still not a value the program holds.
 *
 * An implementation that answered by a node's pointer or number (resolve the ref in the program's
 * root, or in whichever graph it was first asked about) fails the first reading; one that answered
 * by address (cast the handle to the one representation it happens to hold, or key on the address of
 * an adapter) fails the last. Both were run against this pass and are recorded in the lane's
 * report; the lines below are what they turned red.
 *
 *   [PI] graph I holds=1 program=1 nodes=N
 *        graph I of the program: the program holds that value (by identity) and the context's
 *        answer for it is this program. Per graph, so a run that reached nothing cannot pass.
 *   [PI] call graph=I ref=R answer=1 routes=1
 *        one call site: the program answers a callee for it, and the two routes name one value.
 *   [PI] cross graph=I ref=R other=J answer=none
 *        a ref that is not a call in graph J answers no callee for J -- the reading that fails
 *        when the answer comes from another graph's node at that number.
 *   [PI] foreign draft=1 holds=0 program=0
 *        a draft of the program's root, handed over as the same kind of value: the program does not
 *        hold it and the context names no program for it.
 *   [PI] sameness callees=K sites=S shared=G
 *        K graph values answered S call sites, and the largest group of sites that named one value
 *        is G. The plan pins G where its case names one graph twice.
 *   [PI] agree=1 graphs=G calls=C crossed=0 routes=C retained=N foreign=1
 *        every reading above held, with the counts printed so a program that has no call cannot
 *        pass as agreement. `retained` is the node count of the draft read above, so the foreign
 *        reading is one about a real graph and not about an empty one.
 */

#include "camel/passes/test/program_identity.h"

#include "camel/execute/pass/registration.h"
#include "camel/runtime/draft_session.h"
#include "graph/callee_graph_of.h"
#include "graph/gc_graph_view.h"

#include <algorithm>
#include <string>
#include <vector>

namespace {

using camel::graph::NodeRef;
using camel::runtime::GCGraph;

/// The value a reader holds for `graph`: the graph itself, not the `GCGraph *` a dump happens to
/// have. A test that passed the representation would not be asking the question this pass is for.
const camel::graph::Graph &asValue(const GCGraph &graph) { return graph; }

/// True when node `ref` of `graph` is one a call is written on, the places `Program::calleeOf` and
/// `GraphView::calleeOf` both answer for.
bool isCallSite(const GCGraph &graph, camel::runtime::gc_node_ref_t ref) {
    const camel::runtime::GCNode *node = graph.node(ref);
    if (node == nullptr) {
        return false;
    }
    return node->kind == camel::runtime::GCNodeKind::Func ||
           node->kind == camel::runtime::GCNodeKind::Call;
}

} // namespace

const camel::runtime::Program *
ProgramIdentityTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    if (program == nullptr || program->graphs().empty() || context_ == nullptr) {
        return program;
    }
    const auto graphs = program->graphs();
    size_t held       = 0;
    size_t calls      = 0;
    size_t routes     = 0;
    size_t crossed    = 0;
    size_t failures   = 0;
    /// The graph each call site names, one entry per site: two entries that are equal are two
    /// places in the source that named one graph, which is the reading "different text, one value".
    std::vector<const camel::graph::Graph *> siteCallees;
    for (size_t i = 0; i < graphs.size(); ++i) {
        const GCGraph *graph = graphs[i];
        if (graph == nullptr) {
            continue;
        }
        const camel::graph::Graph &value = asValue(*graph);
        // The program holds this value, and the context names this program for it: the two
        // answers the entries give when they are asked by identity.
        const bool holdsIt   = program->holds(value);
        const bool inProgram = context_->programOfRuntimeGraph(value) == program;
        if (holdsIt && inProgram) {
            ++held;
        } else {
            ++failures;
        }
        os << "[PI] graph " << i << " holds=" << (holdsIt ? 1 : 0)
           << " program=" << (inProgram ? 1 : 0) << " nodes=" << graph->nodeCount() << "\n";
        // The view over the same graph, with the program as the resolver: the two routes to a
        // callee -- the program by identity, the view by the record it stores -- must name one
        // value. Built per graph because `FrozenGraphAdapter` holds the resolver it was made with.
        const camel::graph::CalleeGraphOf resolver =
            [program](camel::runtime::GraphId id) -> const camel::graph::Graph * {
            return static_cast<const camel::graph::Graph *>(program->graph(id));
        };
        const camel::graph::FrozenGraphAdapter view(*graph, resolver);
        for (camel::runtime::gc_node_ref_t ref = 0; ref < graph->nodeCount(); ++ref) {
            if (!isCallSite(*graph, ref)) {
                continue;
            }
            const NodeRef node       = static_cast<NodeRef>(ref);
            const GCGraph *answered  = program->calleeOf(value, ref);
            const auto viewCallee    = view.calleeOf(node);
            const bool programNamed  = answered != nullptr;
            const bool viewNamed     = viewCallee.kind == camel::graph::CalleeRef::Kind::Value &&
                                   viewCallee.graph != nullptr;
            const bool sameValue =
                programNamed == viewNamed &&
                (!programNamed ||
                 static_cast<const camel::graph::Graph *>(answered) == viewCallee.graph);
            if (sameValue && programNamed) {
                ++calls;
                ++routes;
            } else if (programNamed) {
                ++calls;
                ++failures;
            }
            if (programNamed) {
                siteCallees.push_back(static_cast<const camel::graph::Graph *>(answered));
            }
            os << "[PI] call graph=" << i << " ref=" << ref
               << " answer=" << (programNamed ? 1 : 0)
               << " routes=" << (sameValue && programNamed ? 1 : 0) << "\n";
            // The same ref asked of every other graph: where that ref is not a call there, the
            // answer must be none. An answer taken from the node's number or pointer shows up here
            // as another graph's callee.
            for (size_t j = 0; j < graphs.size(); ++j) {
                if (j == i || graphs[j] == nullptr || isCallSite(*graphs[j], ref)) {
                    continue;
                }
                const GCGraph *elsewhere = program->calleeOf(asValue(*graphs[j]), ref);
                if (elsewhere != nullptr) {
                    ++crossed;
                    ++failures;
                }
                os << "[PI] cross graph=" << i << " ref=" << ref << " other=" << j
                   << " answer=" << (elsewhere == nullptr ? "none" : "graph") << "\n";
            }
        }
    }
    // The foreign value: a draft of the program's root. It is a `graph::Graph` of this seam and it
    // is not a graph the program holds -- no program does, a draft is what a session is editing.
    size_t retained = 0;
    bool foreign    = false;
    {
        camel::runtime::RuntimeGraphDraftSession session(context_, program);
        camel::runtime::GraphTransaction &transaction = session.edit(program->root());
        camel::runtime::GraphDraft &draft             = transaction.edit();
        retained                                     = draft.nodeSlotCount();
        const bool holdsIt   = program->holds(draft);
        const bool inProgram = context_->programOfRuntimeGraph(draft) == program;
        foreign              = !holdsIt && !inProgram;
        if (!foreign) {
            ++failures;
        }
        os << "[PI] foreign draft=1 holds=" << (holdsIt ? 1 : 0)
           << " program=" << (inProgram ? 1 : 0) << "\n";
    }
    // Two places in the source, one graph: the sites are grouped by the value they named, so
    // "different text, one value" is a count and not an opinion. `shared` is the largest group --
    // the number of call sites that answered with one graph -- and the plan pins it, so a run whose
    // case happened to name a different graph at every site cannot pass as this reading.
    std::vector<const camel::graph::Graph *> grouped = siteCallees;
    std::sort(grouped.begin(), grouped.end());
    size_t distinctCallees = 0;
    size_t sharedSites     = 0;
    for (size_t at = 0; at < grouped.size();) {
        size_t end = at;
        while (end < grouped.size() && grouped[end] == grouped[at]) {
            ++end;
        }
        ++distinctCallees;
        sharedSites = std::max(sharedSites, end - at);
        at          = end;
    }
    os << "[PI] sameness callees=" << distinctCallees << " sites=" << siteCallees.size()
       << " shared=" << sharedSites << "\n";
    os << "[PI] agree=" << (failures == 0 ? 1 : 0) << " graphs=" << graphs.size()
       << " calls=" << calls << " crossed=" << crossed << " routes=" << routes
       << " retained=" << retained << " foreign=" << (foreign ? 1 : 0) << "\n";
    if (held != graphs.size()) {
        os << "[PI] held " << held << " of " << graphs.size() << "\n";
    }
    return program;
}

namespace {

using camel::execute::makePass;
using camel::execute::PassKernel;

/// The test-only pass this file implements, under the name its header declares.
const PassKernel programIdentityKernel(kProgramIdentityTestPass, makePass<ProgramIdentityTestPass>());

} // namespace