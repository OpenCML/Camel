// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/runtime/graph.h"

#include <vector>

namespace camel::transforms {

/*
 * The order a transform reads a body in: **every node after the nodes it reads**.
 *
 * A transform that copies a body node by node (`vmap`, `jvp`, `split`) reads a node's copy out of
 * the copies of its operands, so it has to visit the operands first. The place a node has in a
 * graph is *not* that order, and the difference is not academic:
 *
 *   * the front end builds a body in construction order, so for a graph the compiler just made,
 *     "operand first" and "smaller id first" agree -- and that agreement is why reading the places
 *     in id order looked right;
 *   * a rewrite appends what it writes *after* the nodes it repoints (`src/passes/opt/generic`);
 *     and a frozen graph keeps the ids of the version it came from, so a node a rewrite removed
 *     leaves a `Dead` place with its old id and the replacements sit past the readers they now
 *     feed (`include/camel/runtime/graph.h`, `GCNodeKind::Dead`);
 *   * a draft that is numbered anew (`GraphDraft::keepsNodeIds`, include/camel/runtime/draft.h)
 *     numbers the nodes by their place in the rewritten draft, which is the order the pass wrote
 *     them in and not the order of construction either.
 *
 * So after `std::opt` the ids are neither construction order nor dependency order, and a walk in id
 * order sees a reader before its operand (a refusal at best, a dropped value at worst -- measured on
 * `transpose`, which lost the cotangent of a rewritten product this way).
 *
 * `dependencyOrderOf` answers the order the graph's own edges impose, over the nodes the graph
 * *has*: `Dead` places are not named (they are not nodes), and neither are refs that are not in the
 * graph. Nodes that read nothing are tied with each other, and the tie is taken by **ascending
 * id**, so a graph whose ids already are a topological order -- every graph the front end built --
 * is read back in exactly the order it was read in before this existed. That is the property the
 * readings pinned in the plans rely on: this changes the order only where the old one was wrong.
 *
 * The tie is **not** free to change, and that was measured rather than assumed: flipping it to the
 * larger id first (2026-11-14, the same 1838 tests either way) turns five cells red -- the four
 * backends of `vmap_transform_agrees_with_row_by_row_map`, whose `rules=` reading names the rules in
 * the order the walk asked them (`rules=vmap_rule_mul,vmap_rule_div,...`), and
 * `divide_gradient_both_legs`, which is a known failure whose refusal changes from the divide wall
 * to a later one. The values are right under either tie; the readings and the first wall a program
 * meets are not. Ascending it is, and a walk that needs the opposite direction reverses the order
 * (`reverseDependencyOrderOf`) rather than ruling the ties the other way.
 */
std::vector<camel::runtime::gc_node_ref_t>
dependencyOrderOf(const camel::runtime::GCGraph &graph);

/**
 * The same order the other way round: every node **before** the nodes that read it.
 *
 * A walk that hands values *back* against the edges -- the cotangent walk of `transpose.cpp`, where
 * a node's answer is read by the nodes upstream of it -- needs a node visited before its readers,
 * which is the forward order reversed. That is all this is, so the two walks cannot drift apart and
 * there is one answer to "what order does this graph impose" in the tree instead of two.
 *
 * The tie rule follows from the reversal: the node the forward walk placed **last** comes first,
 * and on a graph whose ids are a topological order -- where the forward order is the id order -- it
 * is exactly the descending sweep the cotangent walk used to do by hand, id for id. So a body the
 * front end built is walked in the order it was always walked in, and a rewritten one is walked in
 * the order the graph imposes instead of the order the pass happened to write.
 *
 * A cycle in the value edges (not a shape this graph model describes: a loop's round feeds back
 * through its region, not through a node input) leaves the nodes the walk could not place at the
 * *front* here, in descending id, where the walk that used to do this by hand left them at the end.
 * Nothing is dropped either way, and the contract that the edges are a DAG is
 * `docs/technical/17_graph_rewrite_api_contracts.md` 4.2.
 */
std::vector<camel::runtime::gc_node_ref_t>
reverseDependencyOrderOf(const camel::runtime::GCGraph &graph);

} // namespace camel::transforms