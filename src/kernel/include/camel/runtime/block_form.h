// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The canonical form of a graph, for naming a block by what its body computes.
 *
 * Design principles 9.5 makes the default graph interface spelling-blind: canonical order is a
 * property of the structure, and names, source positions and creation order are spelling, read only
 * through the `spelling` resource. A block is named after its body, so the name must be a hash of
 * that spelling-free form and of nothing else.
 *
 * This is **not** `fact::portableGraphHash` and must not become it. That one is the key of a cache
 * and of the fact store: it has to tell two programs apart when anything about them differs, so it
 * hashes the graph's name, the node refs in creation order and the operator's traits as stored.
 * This one answers a different question -- "is this the same body?" -- and therefore hashes the
 * opposite: no names, no creation order, no positions; only the structure, read from the graph's
 * exit along its ports. Two contracts, two functions, each with the reader it is written for.
 *
 * What is hashed, and in what order:
 *
 *   - the graph's function type without its parameter names (mangled, which drops them), its port
 *     counts and its return kind;
 *   - then the exit node, and from it, depth first, ports in order (`normInputsOf`, then
 *     `withInputsOf`, then `ctrlInputsOf`), each node by its kind, flags, result type and numeric
 *     permissions -- so the parameters of the body are reached in the order the body first uses
 *     them, which is the order they appear in the sequence;
 *   - an operator by its URI and traits, a constant by its bits, a call by the canonical form of
 *     the graph it names (never by that graph's name), a loop by its kind;
 *   - a node that owns regions, after its ports: the regions in the order the region table names
 *     them (a branch's arms by arm number, a loop's condition before the body it guards), each by
 *     its kind, its number among its owner's regions, the signature of its form ports, every form
 *     port by its position in that region, and the value the region yields -- the statements a
 *     region's exit reads are reached through that value, so a difference that lives only inside a
 *     branch arm or a loop body is a difference in the name;
 *   - a graph already being hashed higher up contributes a marker and its distance, so a recursive
 *     body hashes in bounded time and two different recursive shapes still differ.
 *
 * Parameter identity stays the parameter's position in the signature: a parameter *name* is
 * spelling and does not enter, while `a - b` and `b - a` are two bodies and must not collide. A
 * region's form ports are named the same way, by their position in that region: the state values a
 * loop hands its condition are ports of the condition's region, and a condition that reads one of
 * them is not a condition that reads the other.
 *
 * A region's own node list is never read: that list is in creation order, which is spelling. The
 * region is entered at its form ports and at its exit, which are the parts of it a reader can
 * address, and the statements are reached from the exit. What a region does not reach -- a pure
 * computation nothing reads -- is not part of the body's visible structure, and is hashed nowhere
 * else either.
 */

#pragma once

#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <cstdint>

namespace camel::runtime {

/// The hash of `graph`'s canonical form -- structure only, no names, no creation order -- as a
/// graph of `program`. The same body hashed twice, in two processes, under two module names or with
/// two spellings of its parameters, answers the same value; two bodies that differ structurally
/// answer different ones.
uint64_t blockFormHash(const Program &program, const GCGraph *graph);

} // namespace camel::runtime