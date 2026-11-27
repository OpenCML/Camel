// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The core's kinds of fact in the form the facts framework keeps them (`camel/facts/facts.h`):
 * one file of hooks per kind, registered through `registerKind<T>`, so that a kind says what it
 * computes, what makes two of its values one, how it prints, and where the answer is anchored.
 *
 * The kinds are moving here from `camel/runtime/core_facts.h` one at a time; this header names the
 * ones that have moved. A kind's key does not change with the move (`core.shape`), and the two
 * registries are apart -- the runtime's `FactStore` keeps answering the kinds it has while the
 * store on this side answers the ones that moved.
 */

#pragma once

#include "camel/facts/facts.h"

namespace camel::facts {

/// The kind `core.shape`: the shape of the value a node produces, as the type of its node states
/// it. Anchored on the node, derived from the node's own record, and no merge -- see the
/// registration for why a union of two shapes is not a shape.
KindId shapeKind();

/// The kind `core.permissions`: the numeric permissions the program granted at a node -- the one
/// example the framework's own definition of `Class::Content` names, so it is the kind that states
/// what a Content kind is. Read from the view every time and never stored, so never stale; no merge
/// (see the registration for the reason).
KindId permissionsKind();

} // namespace camel::facts