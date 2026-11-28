// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The signature a transposition is written from: the operator's own type, and the shape readings a
 * law needs when no program wrote the call -- the one `int[]` extents type, the extents a stated
 * shape writes, and the parameter whose declaration states no shape while its value is still read.
 *
 * `transposeSignature` itself is the transform's contract and is declared in `transpose.h`; what
 * lives here is the reading that contract, and every law, is written from.
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/core/type/composite/array.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"
#include "graph/graph_view.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace camel::transforms {

/// The type of `transpose(f)`: `(f: (..) => r) => (cotangent: r) => (..cotangents)`.
camel::core::type::FunctionType *transposeOperatorType(
    camel::core::type::FunctionType *function);

/// The one `int[]` type an extents value has: `extent_of::<A>()` answers `int[]` and nothing else.
camel::core::type::ArrayType *intArrayTypeOf();

/// The extents a type states, written into `draft` as the `int[]` value `extent_of::<A>()` would
/// answer; `kInvalidNodeRef` when the type leaves one of them open.
camel::runtime::gc_node_ref_t statedExtentsNodeOf(
    camel::runtime::GraphDraft &draft, camel::core::type::Type *type);

/// The parameter whose declaration states no shape and whose value a law has to read, found by
/// walking back from `value` to a port of `function`; nothing when no such parameter answers. The
/// walk asks the view for a node's type, kind, ports and inputs, and for nothing else.
std::optional<size_t> unstatedParameterBehind(
    const camel::graph::GraphView *function, camel::runtime::gc_node_ref_t value,
    const std::vector<size_t> &portParameter, size_t paramCount, size_t nodeCount);

/// Which parameters take a port, in port order: `normPorts()` position `i` belongs to the parameter
/// `portParametersOf(...)[i]`. **This is the one place the rule is written.** A parameter takes a
/// port when it is an argument the half is handed: a `()` place is an answer the half gives and
/// never an argument it takes (`split.cpp#buildSplitGraph`: "a () slot is an answer this half
/// gives, never an argument it takes"), so it takes none; every other parameter takes one -- a
/// discrete parameter included, whose place is its own type and whose value is the zero of that
/// type (design principles 9.9).
///
/// Both halves read this list and neither derives it again: `buildSplitGraph` declares the half's
/// ports from it, and `walk.cpp`'s `portOfParameter` says which parameter a port belongs to. The
/// two used to answer it separately, and answered differently the moment a discrete place took a
/// port: a port's position and its parameter's position then differed by one, and the walk read one
/// parameter's cotangent into another's member.
std::vector<size_t> portParametersOf(const camel::core::type::FunctionType *function);

} // namespace camel::transforms