// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `Graph`: the graph, as a value (design principles 2.3: in the process a graph's identity is the
 * value itself, not a number; graph data model 4.4).
 *
 * `graph_view.h` declares this name and says why it is declared there and not defined: a reader of
 * the view names a graph (the callee of a FUNC node, the anchor of a fact) and must be able to do
 * so without learning what a graph is made of. This header is the other half -- the definition --
 * and it is deliberately one line of interface and nothing else:
 *
 *   - today's two representations, the frozen graph (`GCGraph`, camel/runtime/graph.h) and the
 *     graph an editing session is building (`GraphDraft`, camel/runtime/draft.h), each *derive*
 *     from it, so that a `const Graph *` can name either one and neither is the other's rename;
 *   - the base holds no member and no method but the virtual destructor. Everything a graph
 *     answers is asked through `GraphView` (`graph_view.h`), never through this type: a member
 *     here would be a second place a graph can be read from, which is what the view exists to
 *     prevent.
 *
 * Why a base and not a rename (the graph lane's question of 2026-11-22, answered by the author:
 * a graph is a base class, and the two representations derive from it): there are two
 * representations this stage, and a draft's callee has to be nameable by the same `const Graph *` a
 * frozen graph's is. A rename would leave the draft side with no type to point at, and stage 3 --
 * which is where the two representations become one -- is the change that removes the seam, not
 * this one.
 *
 * Stage 3 deletes both representations in favour of one and this header goes with the adapters:
 * what remains is that a graph's identity is the value, which is then the single class itself.
 */

#pragma once

namespace camel::graph {

class GraphView;

/// The graph, as a value: an opaque identity, one way back to the view, and nothing else. See the
/// file comment.
class Graph {
  public:
    virtual ~Graph() = default;

    /// How a reader that holds a `Graph *` -- the graph a `CalleeRef` names, the anchor of a fact
    /// -- reads that graph: the view of it, which is the only face a graph is read through. The
    /// representation answers it with its own adapter today (`FrozenGraphAdapter`,
    /// `DraftGraphAdapter`) and with itself after stage 3c, when the two representations are one.
    ///
    /// The reference stays valid as long as the graph does: it is made on the first call and kept
    /// by the graph, so a caller asks as often as it likes and does not own anything. What this
    /// face cannot answer by itself is a *callee*: naming the graph a FUNC node calls needs the
    /// program that holds the graphs (`CalleeGraphOf`, graph/callee_graph_of.h), and a graph does
    /// not know its program. A reader that has the program's resolver asks `calleeOf` through an
    /// adapter it builds with that resolver; one that does not gets every other answer here and a
    /// refusal on that one rather than a wrong answer (the adapter's `calleeOf` says so).
    virtual const GraphView &view() const = 0;
};

} // namespace camel::graph