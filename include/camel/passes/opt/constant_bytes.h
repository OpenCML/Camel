// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The bytes a constant occupies in a graph. `fitsConstantBudget` (generic.cpp) asks it to decide whether a fold
 * materializes a value or leaves the computation that builds it, and the `graph` module answers `constant_bytes`
 * with it, so that the Camel phase measures the same quantity as the native pass.
 */

#pragma once

#include <cstddef>

#include "camel/core/slot.h"

namespace camel::core::type {
class Type;
} // namespace camel::core::type

namespace camel::passes::opt {

/// The bytes a value of `type` occupies once it is a constant of a graph: its elements plus the shape that
/// describes them. The walk follows members to a depth of 8 and stops once it passes `stopAt` -- a caller only
/// asks whether the value fits -- so a value shared with itself, or a large one, is not walked whole.
size_t constantBytesOf(
    slot_t value, const camel::core::type::Type *type, size_t stopAt, size_t depth = 0);

} // namespace camel::passes::opt