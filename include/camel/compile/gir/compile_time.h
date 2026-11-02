// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Value-level transforms the front end answers while it builds the caller's graph.
 *
 * `jvp(f)`, and the transforms written with it, are calls whose value is a *function*: an operator
 * answers one by building a graph of its own when the call runs (modules/transforms/jvp_operator.cpp).
 * That graph can be built while the caller's graph is being built instead, and the value it stands
 * for is then a constant of the program -- which is what "instantiated at compile time" means here:
 * the graph exists before anything runs, and the call site holds the value rather than a call that
 * makes one.
 *
 * The transform is a module's (it needs the module's builder and its ruleset), so it registers
 * itself here and the front end asks the table. What the front end promises a handler is the call
 * it built -- the graph, the node, the context -- and nothing else: the handler decides whether
 * the shape is one it can answer, answers the whole thing (the value node it stands for) or says
 * no. A handler that says no leaves the call as it was, so the shape keeps the reading it had.
 *
 * This is not a second operator table: the table is keyed by the operator's URI and holds one
 * entry per transform, and an operator that is not in it is answered the way it always was.
 */

#pragma once

#include "types.h"

#include "camel/core/context/context.h"

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <string_view>

namespace camel::compile::gir {

/// One call the front end built, offered to the transform that can answer it.
struct CompileTimeCall {
    const camel::core::context::context_ptr_t &context;
    /// The graph the call was written in, and the OPER node it built there.
    graph_ptr_t caller;
    draft_node_ref_t node = 0;
    /// Where the transform's readings go: the program's own output, as a pass's readings do.
    std::ostream &os;
};

/// A handler: answers the value node the call stands for in `call.caller`, or nothing when this
/// shape is not one it instantiates. A handler that answers has already said everything it has to
/// say in `os`; one that declines has said nothing (a refusal is the run-time path's to report).
using CompileTimeTransform = std::function<std::optional<node_handle_t>(CompileTimeCall &call)>;

/// The hand-over every handler ends with: `node` (the call) becomes `value` (the node that stands for
/// what it answered), and `node` goes.
///
/// The value takes the call's place in the graph, its place in a statement chain included. A call
/// written as a statement of a `sync` block is a link of that block's chain (the `sync` edge and the
/// control edge written beside it), and the statements after it follow it: a value that only reads
/// the call's data users would leave the chain broken at the constant, and the statements written
/// after the call would stop being statements of a block -- which is what decides whether they run
/// at all (the effect rule, include/camel/runtime/effect_liveness.h). So the value inherits the
/// call's `sync` and order predecessors, the mark that says it is a statement, and the edges of both
/// directions, and the call is erased after every one of them points at the value.
void replaceCallWithValue(graph_ptr_t caller, draft_node_ref_t node, draft_node_ref_t value);

/// Registers `transform` for the operator `uri` (`transforms:jvp`). One transform per URI: a
/// second registration is the same kind of defect as two operators under one name, and is refused.
void registerCompileTimeTransform(std::string uri, CompileTimeTransform transform);

/// The handler for `uri`, or nullptr when no module instantiates that operator at compile time.
const CompileTimeTransform *compileTimeTransformOf(std::string_view uri);

/// The calls the handler answered in this program, for the readings and for the A/B: how many
/// calls were lifted before the program ran. Reset by the front end when a program build starts.
void resetCompileTimeInstantiations();
size_t compileTimeInstantiationCount();

} // namespace camel::compile::gir