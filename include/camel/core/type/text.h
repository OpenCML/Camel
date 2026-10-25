// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The text, the mangled name and the structural equality of a type, with the type's own depth
 * walked on an explicit stack (src/core/type/text.cpp).
 *
 * A type is a tree of types: an array holds its element type, a tuple and a struct their
 * fields, a function its parameters and result, a `Map`/`Set` its arguments, an enum instance
 * its type arguments. A recursive enum closes the graph through its *fields*, but its text and
 * its equality follow only its type arguments, so the walk is a DAG whose depth is the nesting
 * written in the program (or produced by instantiating generics), not the depth of any value.
 * That depth is still unbounded in principle, and the three helpers below therefore do not
 * recurse natively.
 *
 * A composite's `toString`/`mangle`/`equals` delegates here. A type that is not one of the
 * built-in composites (a primitive, a type variable, a module type such as Tensor or Json)
 * answers by its own virtual method, as before.
 */

#pragma once

#include <string>

namespace camel::core::type {

class Type;

/// The type as a program writes it: `int[]`, `(a: int) => int`, `Map<string, int>`,
/// `Option<int>`, `var int`.
std::string typeToString(const Type *type);
/// The name that identifies the type by its structure: `Ai64`, `P2i64i64`, ...
std::string typeMangle(const Type *type);
/// Structural equality of two types (the same relation as `Type::equals`).
bool typeEquals(const Type *a, const Type *b);
/**
 * The same relation read off the type table instead of walked: two types are the same type when
 * their canonical ids (camel/core/type/intern.h) are one. That id erases the naming words of a
 * key, so `f32[n]` and `f32[?]` answer yes here, and an object built apart for one structure
 * answers yes as well -- which is the comparison the graph, its content hashes and the e-graph
 * make, since a type has no identity but its structure.
 *
 * A type the table does not hold has no canonical id: two such types are one only when they are
 * the same object (`typeEquals` is the one that still walks them).
 */
bool sameCanonicalType(const Type *a, const Type *b);

} // namespace camel::core::type