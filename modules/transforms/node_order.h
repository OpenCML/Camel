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
 */
std::vector<camel::runtime::gc_node_ref_t>
dependencyOrderOf(const camel::runtime::GCGraph &graph);

} // namespace camel::transforms