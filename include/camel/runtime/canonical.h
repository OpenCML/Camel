// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The canonical order of the nodes of a frozen graph (docs/design-principles.cn.md 9, "the graph
 * interface does not see the spelling").
 *
 * The default queries of the graph interface are independent of how the program was written:
 * `nodes` and the documents built on it return the nodes in one order, and the default numbering
 * is that order's. The order is the graph's, and it is derived from the graph alone:
 *
 *  - It starts at the graph's exit node and at each region's statements, and walks the data,
 *    sync and order edges backwards depth first. A node's ports are visited in order: its normal
 *    inputs, its with inputs, its control (wait) inputs, then its sync and then its order
 *    predecessors. A control node's regions are entered after its ports, in region order, at the
 *    region's result. External accesses are statements, so they are reached this way as well: a
 *    region's statements are the nodes nothing reads, and they are numbered in that region.
 *
 *  - Where the ports do not determine an order there is a tie, and this is the rule that replaced
 *    the order nodes were created in. The ties are the accesses nothing orders (two `unordered`
 *    accesses, two final writes of different captured variables), the order of a capture table --
 *    the values a closure captured sit in its with inputs, and a closure graph's capture ports, in
 *    the order the source captured them in, which is spelling -- and, generally, any two things a
 *    region holds and no edge orders. A tie group is arranged so that its numbering table is the
 *    smallest, and the table of a tie is taken over the whole program: a graph whose numbering
 *    reaches its smallest table under several arrangements does not fix one of them before its
 *    binders are numbered. A binder -- the FILL that makes a function value, the FUNC that calls a
 *    graph -- reads the graph it binds through that graph's ports, by their numbers, so a binder's
 *    key pairs each value it passes with the number of the bound port it goes to; the arrangement
 *    of the bound graph is therefore chosen with the binder's table in view, not before it. That is
 *    why the numbers are taken together for a program (CanonicalProgram) rather than one graph at a
 *    time, and why two isomorphic bindings then carry the same numbers (design principles 9.5, the
 *    second counterexample of the ECOOP specification's 6.4). Where several arrangements of a bound
 *    graph are equally small and no binder tells them apart, they are interchangeable to every
 *    reader of those numbers -- the readers are told apart by the same key -- so none of them can
 *    make two spellings differ, and the group is reported by ties() rather than broken by a choice
 *    the source made. The refinement a merge of structurally equivalent subgraphs needs is the same
 *    cut, applied inside the same solve, so it is the minimum over the parts that are still equal.
 *
 *  - A structural hash is not enough for this comparison: two nodes whose hashes are equal are not
 *    therefore interchangeable, and the two arrangements then need a decision the hash cannot give.
 *
 *  - A tie group is first cut by the node key: nodes whose keys differ are already ordered by the
 *    key, and only the part the key leaves equal -- the nodes that are the same thing, down to the
 *    numbers their ports carry and the type of the value they make -- is enumerated. The cost is
 *    linear in the graph's size when nothing is tied and exponential only in the width of that
 *    equal-key part, which the enumeration limit kMaxTieWidth bounds: a wider part is not
 *    enumerated, and it is not silently arranged in the order the nodes came in either -- it is
 *    cut once more by what each node writes on its own (its own fragment table, and the numbers of
 *    the nodes that read it), and a part that is still equal at that point is reported by
 *    CanonicalProgram::ties() (`camel inspect --ties`) as a known limitation, with its width.
 *
 *  - A node the walk does not reach (a dead write, a computation whose value nothing uses) is
 *    placed after everything the walk does reach, one region at a time, by the same tie rule. It
 *    is not part of the default query of a program; a document that prints the graph still prints
 *    it, and this is where.
 *
 * The version ports the design principles name as a further tie source do not exist as nodes in
 * this graph model, so nothing here arranges them; the bindings whose key has no number yet are
 * the same kind of missing thing. They become tie groups when the graph has them.
 */

#pragma once

#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace camel::runtime {

/// A tie part wider than this is not enumerated: the arrangements of a part that wide are not
/// compared one by one (6! is 720). Such a part is cut by the nodes' own fragments first, and what
/// stays equal is reported by CanonicalProgram::ties() instead of being arranged silently.
inline constexpr size_t kMaxTieWidth = 6;

/// One tie the walk met while numbering a graph: how many nodes the ports left unordered, how much
/// of that the node key left equal, and whether that equal part was enumerated.
struct CanonicalTie {
    const GCGraph *graph       = nullptr;
    size_t width               = 0; // nodes the ports left unordered
    size_t equalKeyWidth       = 0; // the widest part of it the node key left equal
    bool enumerated            = false;
};

/// The canonical numbering of a program: every graph the root reaches, numbered together so that a
/// tie is arranged with the binders that read it in view (design principles 9.5).
///
/// The solve runs in dependency order: the graphs a graph binds are numbered first (their own ties
/// arranged the same way), and a binder then chooses among its own arrangements with the numbers of
/// the bound graph's ports already fixed. A graph no binder distinguishes keeps the arrangement its
/// own keys give, which is the same arrangement whatever order the source made it in; when several
/// such arrangements are equally small they are interchangeable to every reader, and the group is
/// reported by ties() rather than decided by the order the nodes were created in.
class CanonicalProgram {
  public:
    CanonicalProgram(const Program &program, const GCGraph &root);

    /// The nodes of `graph` in canonical order; empty for a graph this solve does not hold.
    const std::vector<gc_node_ref_t> &order(const GCGraph &graph) const;
    /// The number each node of `graph` has, indexed by NodeId (kNoCanonicalNumber where it has
    /// none).
    const std::vector<uint32_t> &numbers(const GCGraph &graph) const;
    /// The tie groups the numbering met, widest first.
    const std::vector<CanonicalTie> &ties() const { return ties_; }
    /// The tie groups as text: one line per group wider than one node, and a summary line. What
    /// `camel inspect --ties` prints, and the report of a part wider than kMaxTieWidth.
    std::string tiesText() const;

  private:
    struct GraphNumbers {
        std::vector<gc_node_ref_t> order;
        std::vector<uint32_t> numbers;
    };

    std::unordered_map<const GCGraph *, GraphNumbers> graphs_;
    std::vector<CanonicalTie> ties_;
};

/// The nodes of `graph` in canonical order: every live node exactly once.
std::vector<gc_node_ref_t> canonicalNodeOrder(const Program &program, const GCGraph &graph);

/// The number each node of `graph` has in the canonical order, indexed by NodeId; a NodeId that
/// names no live node (or is past the graph's places) has kNoCanonicalNumber.
inline constexpr uint32_t kNoCanonicalNumber = static_cast<uint32_t>(-1);
std::vector<uint32_t> canonicalNodeNumbers(const Program &program, const GCGraph &graph);

} // namespace camel::runtime