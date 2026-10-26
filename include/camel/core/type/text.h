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
 * That depth is still unbounded in principle, and the helpers below therefore do not
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
 * Whether `target` and `source` are the same type, where a function type that writes no resource
 * clause is the same type as one that writes them: the clause says what a value of the type
 * touches, and a type that writes none says nothing about resources, so it takes the resources of
 * the type it is compared with. This is what a function-typed *parameter* written without a
 * clause means (`f: (x: int) => int` takes a callback that touches anything), and what such a
 * clause anywhere else in a written type means as well, since nothing else determines it. A
 * clause that is written on the target side is compared as it stands, so an argument whose
 * resources are not known does not fit a promise of `pure`; `covers`
 * (camel/core/resource_semantics.h) is the same relation between two clauses.
 *
 * Every difference other than that one -- another element type, another shape, another result,
 * another written resource clause, a nesting of any of these -- is the difference `typeEquals`
 * reports. Use it where a written type meets the type of a value; the identity of a type
 * (`Type::equals`, the intern table, the graph's hashes) is `typeEquals`.
 */
bool sameTypeWithUnwrittenResources(const Type *target, const Type *source);
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