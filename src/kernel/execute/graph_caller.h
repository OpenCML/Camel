// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Running a graph on the reference executor, for a caller that holds no function value.
 *
 * A pass written in Camel is one such caller: the pipeline compiles the pass's module and keeps
 * its tool program, and running that program's graphs is the interpreter's work, not the
 * pipeline's. So this is an interface, not a dependency: the reference executor implements it
 * (`src/passes/sched/nodevm/nodevm.cpp`) and installs it while the program loads, and
 * `src/execute/camel_pass.cpp` never names the executor it runs a pass on
.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/slot.h"
#include "camel/runtime/graph.h"
#include "camel/runtime/program.h"

#include <span>

namespace camel::execute::detail {

/// Runs `graph` of `program` with `arguments` for its ordinary parameters and `withArguments` for
/// its `with` parameters, and returns its value. The caller owns what it passes: `program` must
/// stay alive and rooted for the call, and so must the arguments. A body that captured values
/// reads them from the closure nodes of its graph, which only a caller holding the function value
/// can fill; a caller that passes no closure leaves them empty.
using GraphCaller = slot_t (*)(
    const camel::core::context::context_ptr_t &ctx, const camel::runtime::Program *program,
    camel::runtime::GCGraph *graph, std::span<const slot_t> arguments,
    std::span<const slot_t> withArguments, std::span<const slot_t> closure);

/// Installs the caller. Called once, by the reference executor, while the program loads.
void setGraphCaller(GraphCaller caller);

/// The installed caller, or null before the reference executor has loaded.
GraphCaller graphCaller();

} // namespace camel::execute::detail