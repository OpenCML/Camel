// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The cotangent side of a transposition, asked of the type system and of the derivative registry:
 * the type a cotangent input lives in, the addition a cotangent space sums with, the zero it starts
 * from, and whether a parameter's place holds a cotangent at all.
 *
 * The reading of a graph's parameter list belongs here too: `transposedPlacesOf` is what a
 * transposed signature is written from, and `lift.h#liftedParametersOf` is the law it reads.
 */

#pragma once

#include "lift.h"

#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"
#include "camel/runtime/draft.h"
#include "camel/runtime/graph.h"
#include "graph/graph_view.h"

#include <string>

namespace camel::transforms {

/// The addition a cotangent space sums with, as the registry spells it; empty when the space
/// registers none.
std::string addUriOfSpace(camel::core::type::Type *space);

/// The addition the cotangent space of the value at `ref` sums with, with the space itself written
/// to `addType`; empty when the type has no space or the space registers no addition. The value's
/// type is the one reading of a graph this asks, so it is asked of the view
/// (`graph/graph_view.h`) and not of a representation.
std::string addUriFor(
    const camel::graph::GraphView *function, camel::runtime::gc_node_ref_t ref,
    camel::core::type::Type *&addType);

/// Why this transform has no cotangent for a result of type `result`, in the words the call site's
/// diagnosis uses; empty when it has one.
std::string resultCotangentRefusalImpl(const camel::core::type::Type *result);

/// Whether this transform has a cotangent for a result of type `result`.
bool hasResultCotangent(const camel::core::type::Type *result);

/// The type a transposed function's cotangent input lives in, or nullptr for a result with no
/// tangent space.
camel::core::type::Type *cotangentTypeOfResultType(const camel::core::type::Type *result);

/// The cotangent input of the transposition of a function of this type.
camel::core::type::Type *resultCotangentType(const camel::core::type::FunctionType *function);

/// Whether `type` is the unit tuple `()`: a place that holds no cotangent and no value.
bool isUnitType(const camel::core::type::Type *type);

/// Whether the function a lifted signature was lifted from eats only tangents.
bool liftSourceEatsOnlyTangents(const LiftedParameters &places);

/// The reading this file takes of a graph's parameter list, given whether the signature is the
/// lifted one.
LiftedParameters transposedPlacesOf(
    const camel::core::type::FunctionType *type, bool liftedSignature);

/// The zero a cotangent space starts from, built in `draft`, or `kInvalidNodeRef` when the space
/// has none.
camel::runtime::gc_node_ref_t zeroOfSpaceOf(
    camel::runtime::GraphDraft &draft, camel::core::type::Type *space,
    camel::runtime::gc_node_ref_t shapeFrom);

} // namespace camel::transforms