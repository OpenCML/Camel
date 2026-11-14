// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The batching transform of a function graph: `reinterp<vmap>(f)`.
 *
 * `reinterp` produces a *new function*, not a rewritten graph (the author's ruling a824031aa, the
 * same one `jvp` follows): the transform builds a graph of its own and hands it to the call site as
 * a function value. The ruleset is `stdlib/rulesets/vmap.cml`, whose `Lift<T>` is `RowsOf<T>`: a
 * value with one dimension of unknown length in front of it (`f32` holds a batch of `f32` rows).
 *
 * Why this reads a rule differently from `jvp`, and why that is not a second mechanism: a rule's
 * right side is two arrows, the outer one taking the primitive's operands and the inner one taking
 * the lifted half. In a `jvp` graph the operands exist (the value chain is copied, and only the
 * tangent is the rule's), so both arrows are called. In a `vmap` graph they do not exist: `Lift<T>`
 * replaced the whole chain with batches, so an unbatched operand is a value the graph has none of,
 * and handing a batch to the outer arrow is a type error. So the transform reads the *name* the
 * rule's right side calls (`rhsCalleeOf`, include/camel/parse/ast/rule_head.h) and calls that
 * function with the batches as its arguments. The rule mechanism is unchanged; what differs is
 * which half of a rule a transform can ask for.
 *
 * A family other than the one an operator's key was first recorded for is read through that *key*:
 * The key a batching rule is filed under -- the spelling the rule's left side writes, and how many
 * operands that spelling takes -- is read off the operator itself (`camel/core/forward.h#ruleKeyOf`):
 * this transform keeps no table of its own, and the two operators whose key cannot be derived are
 * named, with their reasons, in `ruleKeyExceptions()`.
 * reads it and `lookupRule("rulesets.vmap", head, arity)` asks this family.
 *
 * What is built today is the elementwise tier, `matmul`, the reductions along an axis, and calls: a
 * body of ports, constants, primitives that have a rule, and calls, which are *asked about* -- by the
 * callee's name and its argument count, the key a call already has -- like any primitive. A primitive
 * with no rule, a call the family has no rule for, and any other construct (a branch, a loop) is
 * refused *by name* at the node it was written at -- never copied and hoped for (design principles
 * 9.9). The count is part of the answer: `asked=1 found=0 missing=1` at a call says the family was
 * asked and had nothing, which is a hole somebody can fill; `asked=0` would say the walk never
 * arrived. A reduction is batched in terms of the axis it was given (`sum_axis` at one axis further
 * in), and the reductions over every axis (`tensor:sum`) have no batching rule: a batch of rows under
 * them is not the rows' sums, and the whole-array form takes no axis to shift. `reshape` is the next
 * tier.
 */

#pragma once

#include "camel/core/context/context.h"
#include "camel/core/rtdata/tuple.h"
#include "camel/core/source/ids.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/graph.h"

#include <iosfwd>
#include <string>
#include <vector>

namespace camel::transforms {

/// The graph of `reinterp<vmap>(f)` and the program of the graphs built with it.
///
/// `program` is held through `ProgramHold`: the group that reserved it is gone by the time this
/// value reaches the caller, a program reserved while a program runs is one `reserve` does not
/// root, and a safepoint between the two would free the program `graph` is a graph of.
struct VmapGraph {
    camel::runtime::ProgramHold program;
    camel::runtime::GCGraph *graph = nullptr;
    /// Where the build stopped, when `graph` is null: the primitive (or construct) the transform
    /// could not lift, as the origin of that node in `function`.
    camel::source::origin_id_t refusedOrigin = camel::source::kInvalidOriginId;
    std::string refusedUri;
    std::string refusedWhy;
    std::string refusedConstruct;
    /// The reading of the ruleset: how many primitives were asked about, how many of them the
    /// family had a rule for, and which rules those were, in the order the walk asked. A ruleset
    /// that is written is not a ruleset that is used, so the count is part of what the transform
    /// answers -- an elementwise body and a body of zero primitives would otherwise look alike.
    size_t asked = 0;
    size_t found = 0;
    std::vector<std::string> rules;
};

/// The signature `reinterp<vmap>(f)` has for `function`: `(RowsOf<T>) => RowsOf<U>` for a function
/// `(T) => U` of one ordinary parameter, or nullptr when the function is outside what the transform
/// builds (a `with` parameter, more than one parameter, or a body it cannot lift).
camel::core::type::FunctionType *vmapSignature(camel::core::type::FunctionType *function);

/// Builds the batched graph of `function` and prints what it built.
///
/// `captured` is the closure tuple of the function *value* being transformed: the values the
/// function captured where it was written. They are not the graph's to know -- a graph the front end
/// builds records its captures by port and not by value -- so the caller that holds the value hands
/// them in. Each one enters the batched graph as a constant, in the source graph's capture order, so
/// a body that reads a capture reads that value and not the batch element.
VmapGraph buildVmapGraph(
    const camel::core::context::context_ptr_t &context, const camel::runtime::Program *program,
    camel::runtime::GCGraph *function, const ::Tuple *captured, std::ostream &os);

} // namespace camel::transforms