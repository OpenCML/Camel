// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Compile-side Graph -> GCGraph export entrypoints.
 *
 * Runtime graph materialization is a compile-side responsibility. Keep the
 * API here rather than on `Graph` itself so the mutable compile graph object
 * does not own runtime-export behavior.
 */

#pragma once

#include "types.h"

namespace camel::runtime {
class Program;
class GraphIdAllocator;
}

namespace camel::compile::gir {

const void *compileGraphOpaque(const graph_ptr_t &graph);
/// Encodes the graphs reachable from `rootGraph` into runtime graphs and binds them in
/// `program`, with the entry as its root. A runtime graph has the GraphId of the compile graph it
/// comes from; with `freshIds` it takes a new one from there instead, for a second encoding of
/// compile graphs that a program holds already (the definition graphs of operators), so that
/// one program never holds two graphs of an id.
void encodeToRuntimeProgram(
    const graph_ptr_t &rootGraph, camel::runtime::Program &program,
    camel::runtime::GraphIdAllocator *freshIds = nullptr);

} // namespace camel::compile::gir
