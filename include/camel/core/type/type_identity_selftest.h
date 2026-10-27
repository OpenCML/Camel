// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The self-test of type identity (`std::type::identity`): one identity per type, and one table for
 * `as`. Over a corpus that walks every type constructor it checks that equality is reflexive and
 * symmetric, that equal types are assignable both ways and share a canonical id, that nothing
 * is assignable from an unknown source, that `void` discards only in the result of a function, that
 * a function type is told apart by what it consists of, that the least common type of two
 * branch arms exists for tuples, structs, `T?`, `T!` and functions, and that `as` between two
 * different primitive types is refused.
 */

#pragma once

#include <string>

namespace camel::core::type {

/// Runs the self-test; returns the one-line summary the pass prints. Throws std::runtime_error
/// on the first broken invariant.
std::string debugRunTypeIdentitySelfTest();

} // namespace camel::core::type
