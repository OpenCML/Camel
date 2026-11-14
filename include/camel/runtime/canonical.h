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
 *    smallest: every arrangement is numbered and the arrangements are compared by the list of the
 *    nodes they emit, in numbering order, each node described by what it is (its kind, its
 *    operator's identity, the value a constant holds, the role of a port) and by the numbers its
 *    ports already carry. The smallest list wins, and two arrangements with one table keep the
 *    first. A structural hash is not enough for this comparison: two nodes whose hashes are equal
 *    are not therefore interchangeable, and the two arrangements then need a decision the hash
 *    cannot give.
 *
 *  - The cost is linear in the graph's size when nothing is tied, and exponential in the width of
 *    a tie: a group of at most kMaxTieWidth nodes is arranged by enumerating its arrangements
 *    (6! = 720 of them), and one wider than that is arranged by the same node key taken one node
 *    at a time, which is the same answer wherever that key distinguishes the nodes and is the only
 *    deterministic answer where it cannot.
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
#include <vector>

namespace camel::runtime {

/// A tie wider than this is arranged one node at a time instead of by enumerating every
/// arrangement: 6! is 720, and a group that wide is not a capture table.
inline constexpr size_t kMaxTieWidth = 6;

/// The nodes of `graph` in canonical order: every live node exactly once.
std::vector<gc_node_ref_t> canonicalNodeOrder(const Program &program, const GCGraph &graph);

/// The number each node of `graph` has in the canonical order, indexed by NodeId; a NodeId that
/// names no live node (or is past the graph's places) has kNoCanonicalNumber.
inline constexpr uint32_t kNoCanonicalNumber = static_cast<uint32_t>(-1);
std::vector<uint32_t> canonicalNodeNumbers(const Program &program, const GCGraph &graph);

} // namespace camel::runtime