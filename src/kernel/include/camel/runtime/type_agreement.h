// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The relation a value and the place it goes into have to hold.
 *
 * It is not a question about a graph: it takes two types and answers whether one may stand where
 * the other is expected, and the graph header declared it only because the seal that asks it is
 * written there. The seal's edge check, a pass judging a call before it writes it, and a test
 * that checks a conversion all ask the same question, so it is stated once here.
 */

#pragma once

namespace camel::core::type {
class Type;
} // namespace camel::core::type

namespace camel::runtime {

/// The relation a value and the place it goes into have to hold: equal, or one refines the other
/// (a tensor of a known shape and the tensor of the same element type whose shape is not known),
/// or either may be assigned to the other. Parameter and field names do not count, and `void` is
/// compatible with everything, because it is a value nobody reads.
///
/// This is the one spelling of "the types agree". It is the judgement the seal
/// makes along an edge (docs/technical/17_graph_rewrite_api_contracts.md, 4.2: a call's arguments
/// and result are compatible with the callee's parameters and result), and it is the judgement a
/// pass asks when it judges a call before writing it -- the same question, asked once, so the
/// answer a pass works from and the answer the commit gives cannot drift apart.
bool typesCompatible(camel::core::type::Type *a, camel::core::type::Type *b);

} // namespace camel::runtime
