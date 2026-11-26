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
 *   - a graph already being hashed higher up contributes a marker and its distance, so a recursive
 *     body hashes in bounded time and two different recursive shapes still differ.
 *
 * Parameter identity stays the parameter's position in the signature: a parameter *name* is
 * spelling and does not enter, while `a - b` and `b - a` are two bodies and must not collide.
 *
 * Known gap, stated rather than hidden: the nodes *inside* a region (a branch arm, a loop body) are
 * not walked -- the region's node list is addressed by creation order, which is spelling, and there
 * is no structural entry to it here. A body whose difference lives only inside a region and that
 * has the same loop kinds and the same outer nodes therefore hashes the same. Closing it needs the
 * canonical order of 9.5 itself (the numbering that also resolves ties across a program).
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