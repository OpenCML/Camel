// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Type variables and type functions (design principles 5.6).
 *
 * Both exist only while a module is compiled; neither ever reaches a graph.
 *
 * - TypeVarType is a named hole in a type. A generic function declares its type parameters at
 *   its head (`func first::<T>(xs: T[]): T`) and uses them as plain names in its signature; a
 *   parameterised type declaration (`type Pair<T> = (T, T)`) names its parameters the same
 *   way. A generic function is a subgraph template: every call binds the variables from the
 *   argument types and builds a subgraph in which each variable is replaced by its binding.
 * - TypeFunctionType is what a parameterised type declaration declares: parameter names and a
 *   body, an ordinary type expression over them. Applying it (`Pair<int>`) substitutes the
 *   arguments into the body, so a use site only ever holds the resulting type.
 *
 * The free functions are the whole algebra over them: test, substitute, bind.
 */

#pragma once

#include "camel/core/type/base.h"
#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"
#include "camel/core/type/extent.h"

#include <string>
#include <utility>
#include <vector>

namespace camel::core::type {

class TypeVarType : public Type {
  public:
    /// `binder`: this occurrence is one the signature can bind, because it writes a type parameter
    /// the declaration declares at its head. Every other occurrence is a use.
    static TypeVarType *create(const std::string &name, bool binder);

    const std::string &name() const { return name_; }
    bool binder() const { return binder_; }

    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    /// Occurrences of one variable are equal whether or not they bind.
    bool equals(Type *type) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;

  private:
    TypeVarType(const std::string &name, bool binder);

    std::string name_;
    bool binder_;
};

/// The variables bound so far and their types, in binding order.
using type_bindings_t = std::vector<std::pair<std::string, Type *>>;

/// The type bound to `name`, or nullptr.
Type *findTypeBinding(const type_bindings_t &bindings, const std::string &name);

/// The variables a call bound to a sequence of types rather than to one: `..A` given three
/// arguments in a declaration that writes a pack. It is a table of its own beside
/// `type_bindings_t` because only a declaration with a pack has one, and a substitution that
/// knows no pack stays exactly what it was.
using type_pack_bindings_t = std::vector<std::pair<std::string, std::vector<Type *>>>;

/// The sequence of types bound to `name`, or nullptr when no pack of that name was bound. A pack
/// bound to nothing is an empty sequence, not an absent one: it is what a call with no member
/// over that pack gives, and an expansion over it is the empty tuple.
const std::vector<Type *> *findTypePackBinding(
    const type_pack_bindings_t &packs, const std::string &name);

class TypeFunctionType : public Type {
  public:
    static TypeFunctionType *
    create(const std::string &name, std::vector<std::string> params, Type *body);

    const std::string &name() const { return name_; }
    const std::vector<std::string> &params() const { return params_; }
    Type *body() const { return body_; }

    /// The body with each parameter replaced by the argument at its position. The number of
    /// arguments must equal the number of parameters.
    Type *apply(const type_vec_t &arguments) const;

    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    bool equals(Type *type) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;

  private:
    TypeFunctionType(const std::string &name, std::vector<std::string> params, Type *body);

    std::string name_;
    std::vector<std::string> params_;
    Type *body_;
};

/// Whether a type variable occurs anywhere in `type`.
bool containsTypeVar(Type *type);

/**
 * `typeof e` (design principles 5.6): the type of the value `e` will have. It exists only while
 * a module is compiled and only as the whole type written after `typeof`: the graph builder
 * builds `e` where the type is used and replaces the placeholder by the expression's type.
 *
 * The expression is a pointer the compiler owns (a node of the graph construction tree). The
 * core type system never looks at it, so the type stays an opaque, uninterned object here.
 */
class TypeOfType : public Type {
  public:
    static TypeOfType *create(const void *expression);

    const void *expression() const { return expression_; }

    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    bool equals(Type *type) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;

  private:
    explicit TypeOfType(const void *expression);

    const void *expression_;
};

/// Whether a `typeof` placeholder occurs anywhere in `type`.
bool containsTypeOf(Type *type);

/**
 * `type` with every variable that `bindings` binds replaced by its binding; a variable they do
 * not bind stays. Returns `type` itself (the same object) when nothing in it was replaced, so
 * code that holds no bindings is not affected.
 */
Type *substituteTypeVars(Type *type, const type_bindings_t &bindings);

/**
 * The same substitution with the packs a call bound beside the single types. A pack expansion
 * (`..E`, TypePackType) is replaced by the tuple of what `E` becomes for each member of the pack
 * it names, one result per member in binding order; a pack the table does not bind leaves the
 * node waiting, its body substituted as far as the ordinary bindings reach. A pack of `n`
 * members therefore expands to an `n`-member tuple, and an empty pack to the empty tuple.
 */
Type *substituteTypeVars(
    Type *type, const type_bindings_t &bindings, const type_pack_bindings_t &packs);

/**
 * `type` with every named extent that `values` binds replaced by its value (`f32[n]`
 * with n = 4 is `f32[4]`). A `constas` parameter is not substituted: it is a
 * parameter, not an extent. Returns `type` itself when nothing in it was replaced.
 */
Type *substituteExtentValues(Type *type, const ExtentValues &values, const ExtentPacks &packs = {});

/**
 * A new open function type (camel/core/type/composite/func.h): `type` with the variables
 * replaced as substituteTypeVars does, and with its parameter names, `var` marks, modifiers and
 * implementation mark kept. When `extents` is given, its named extents are replaced too and the
 * `constas` parameter those values bind is left out of the result: the instance is called with
 * the runtime arguments only (design principles 5.4). Always a new object, also when nothing was
 * replaced: it is the signature of a graph about to be built, whose result type the body may
 * still decide, so two graphs never share one.
 */
OpenFunctionType *instantiateFunctionType(
    FunctionType *type, const type_bindings_t &bindings,
    const ExtentValues *extents = nullptr);

/**
 * Binds the variables `pattern` binds (occurrences of its head-declared type parameters) from
 * the type `actual` found at the same position, appending to `bindings`. Only binding
 * occurrences are looked at: a variable that is already bound, and every use, is left for the
 * ordinary check of the substituted type against the argument. Returns false when `actual` does
 * not have the shape `pattern` needs to reach a binding occurrence (an array where a tuple is,
 * tuples of different sizes, ...).
 */
bool bindTypeVars(Type *pattern, Type *actual, type_bindings_t &bindings);

/**
 * Collects what `actual` says about the variables of `pattern`: a variable that is not bound
 * yet is bound from the type found at ANY of its occurrences, binding or not (design
 * principles 5.5: the constraints all arguments give are collected, then solved). `actual`
 * may itself still hold variables (the type an argument is expected to have, not yet known in
 * full); a position where it does binds nothing. A `T?` / `T!` in `pattern` met by a type
 * that is not of that form takes the type as its payload (the value enters the form).
 * Returns false when the shapes differ where a variable still had to be reached. Whether the
 * argument fits the parameter is left to the check of the substituted type.
 */
bool unifyTypeVars(Type *pattern, Type *actual, type_bindings_t &bindings);

/// The names of all variables in `type`, binding or not, appended to `names` without
/// duplicates.
void collectTypeVars(Type *type, std::vector<std::string> &names);

/// The names of the variables `pattern` can bind (see bindTypeVars), appended to `names`
/// without duplicates. One inside a union is not among them: there is no position of the
/// argument type to take it from. Among the arguments of a module type it is, when those
/// arguments are plain types (OtherType::typeArguments, e.g. `Map<K, V>`).
void collectBindableTypeVars(Type *pattern, std::vector<std::string> &names);

/// A `T??` or `T!!` anywhere in `type`, or nullptr: the same mark twice, which a type
/// expression rejects where it is written and which substitution can still produce
/// (`T?` with `T = int?`).
TupleType *findRepeatedMark(Type *type);

/// `T = int, U = string`, for diagnostics and instance names.
std::string typeBindingsToString(const type_bindings_t &bindings);

} // namespace camel::core::type

