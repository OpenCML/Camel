// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "graph.h"
#include "program.h"

#include <functional>
#include <vector>

namespace camel::runtime {

/*
 * Runtime graph closure traversal is the canonical reachability API for
 * runtime-facing passes. Scheduling, dumping, linking, and debugging should
 * enumerate GCGraph identity directly (the references of a graph are read in a Program) instead of
 * rediscovering closure through compile-time GIR ownership edges.
 */
std::vector<GCGraph *> collectReachableGraphs(const Program &program, GCGraph *root);
/// The graphs reachable from the root of `program`.
std::vector<GCGraph *> collectReachableGraphs(const Program &program);

/// A number that changes when the revision of `root` or of any graph reachable from it does
/// (a hash of `root`'s id and their (GraphId, revision) pairs in the order of the ids): what a fact or a cache
/// about `root` and everything it refers to is stamped with.
uint64_t closureRevision(const Program &program, GCGraph *root);

void forEachReachableGraph(
    const Program &program, GCGraph *root, const std::function<void(GCGraph *)> &visitor);

} // namespace camel::runtime
