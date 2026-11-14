// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Types computed from types at compile time (design principles 5.6, 21): a type function is
 * written with `type` and called with `F<...>` in a type position, and its body is a type
 * expression, a `match type` among them.
 *
 *   - `TypeCallType` is a type function application whose evaluation is deferred: a primitive
 *     such as `PayloadOf<T>`, `KeyOf<T>` or `ValueOf<T>` (the library declares those without a
 *     body), or a source type function with a `typeof` argument. The latter keeps its declaration
 *     as the first, internal parameter until the argument's type is known.
 *   - `MatchTypeType` is a `match type` in a type position: the subject, and for each case the
 *     shapes and identities it selects and the type it gives.
 *
 * While an argument or the subject is still a type variable or an unresolved `typeof`, the
 * application stays a type of its own, and the type-variable algebra descends through it like any
 * other type application. When everything is concrete, which is when the variables of a generic
 * instance are bound and the graph builder has inferred each `typeof`, `substituteTypeVars` or the
 * graph builder replaces it by the type it computes. One that stays unevaluated with
 * concrete arguments (`ElementOf<int>`, a `match type` no case selects) is reported where the
 * type is used.
 */

#pragma once

#include "camel/core/type/other.h"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace camel::core::type {

class TypeFunctionType;

class TypeCallType final : public OtherType {
  public:
    static TypeCode typeCode();
    static TypeCallType *create(std::string_view name, std::span<Type *const> arguments);
    /// A source type function kept with its arguments until a `typeof` among them is resolved.
    static TypeCallType *
    makeTypeFunctionCall(TypeFunctionType *function, std::span<Type *const> arguments);

    /// Whether `name` is one of the primitive type functions.
    static bool isTypeFunction(std::string_view name);
    /// The type the function computes from concrete arguments; nullptr with `error` set when it
    /// does not accept them.
    static Type *
    evaluate(std::string_view name, std::span<Type *const> arguments, std::string &error);

    const std::string &name() const { return *name_; }

    /// Type packs (design principles 5.6, program transforms): a pack is the application
    /// `$pack<T1, T2, ...>` of its members, what `ParamsOf<F>` and `WithOf<F>` compute and what a
    /// pack variable (`A` of `::<..A>`) is bound to; `..E` written in a type is the application
    /// `$expand<E>`, which becomes one member per element of the packs `E` uses when they are
    /// bound (substituteTypeVars). `$lengths:...` is what an expansion becomes when the packs it
    /// uses have different lengths, and stays so to be reported where the type is used.
    static TypeCallType *makePack(std::span<Type *const> members);
    static TypeCallType *makeExpand(Type *pattern);
    static TypeCallType *makeLengthMismatch(const std::string &description);
    static bool isPack(const Type *type);
    static bool isExpand(const Type *type);
    /// The members of a pack, or of nothing when `type` is not one.
    static std::span<Type *const> packMembers(const Type *type);

    bool typeArguments() const override { return true; }
    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    bool equals(Type *type) const override;
    bool assignableFrom(Type *sourceType) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;

  private:
    TypeCallType(const std::string *name, size_t paramCount, Type **params)
        : OtherType(typeCode(), paramCount, params), name_(name) {}

    const std::string *name_;
};

/// The guard of a `match type` case (`case pattern when predicate => ...`), as the type layer
/// holds it: the compiler's own tree of the predicate, the module it is written in, and how it is
/// written. The type system reads none of the three; it asks the evaluator the build installs
/// (`typevar.h`, `typeCaseGuardEvaluator`), because a predicate is an ordinary Camel function and
/// running one is the compiler's business, not the type layer's (design principles 5.5, ruling of
/// 2026-11-14).
struct TypeCaseGuard {
    /// The compiler's tree of the condition. `void` and not owning, so that this header stays a
    /// type header: it points into the tree the case was built from, which outlives the types of
    /// the program (the build lowers bodies from that tree), and the evaluator casts it back.
    const void *condition = nullptr;
    /// Where the condition is written (the compiler's module), for the names it may call.
    const void *context = nullptr;
    /// The condition as written, for `toString` and for diagnostics.
    std::string text;
};

/// One case of a `match type` in a type position.
struct MatchTypeArm {
    /// The shapes the case names, as bits numbered like `TypeCaseKind` (Struct 0, Tuple 1, Leaf 2,
    /// Wildcard 3, Func 4, Array 6, Nullable 7, Fallible 8, Map 9, Set 10).
    uint16_t shapes = 0;
    /// The types the case names by identity.
    std::vector<Type *> identities;
    /// The patterns the case writes to take a type pack apart, one for each alternative of `|`
    /// (`case (W, ...Rest)`): a tuple type whose members are the pattern's members and whose last
    /// one is `...Rest` (design principles 5.6, where a type pack takes apart the same way a value
    /// pack does). Such a case selects a pack whose members the pattern fits, and the names it
    /// writes stand for those members. The head of a pack is this pattern in the standard library
    /// and nothing else (`stdlib/types.cml` declares it as a `match type`).
    std::vector<Type *> packPatterns;
    /// The patterns the case writes to take an *array* type apart, one for each alternative of `|`
    /// (`case E[...Dims]`, `case E[...]`): an array type with no extent before its dimensions pack
    /// whose element type is the name the case binds (an array whose first dimension is an array
    /// is still one array, so `E` is that element and `Dims` the dimensions after it).
    ///
    /// The two spellings select the two things a shape states. A *named* pack (`case E[...Dims]`)
    /// binds the dimensions themselves, so it selects an array whose rank is stated: the name
    /// stands for the dimensions as a dimension pack, and, where an extent is written, for how
    /// many there are -- what `ShapeOf<T>` needs (`int[Dims]` is `int[<the rank>]`). An
    /// *anonymous* pack (`case E[...]`) is the open rank: the value's rank is not stated, so there
    /// is nothing to bind and nothing to count. Design principles 5.4: `T[...]` is the type of
    /// every rank, and a dimension pack is the dimensions a type does not name.
    std::vector<Type *> arrayPatterns;
    /// The guard the case writes, or null. A case is selected when its pattern matches *and* its
    /// guard holds; one whose guard does not hold selects nothing and the case after it is tried.
    std::shared_ptr<const TypeCaseGuard> guard;
    /// The type the case gives.
    Type *result = nullptr;
};

/// The shapes a concrete type has, as the bits of `MatchTypeArm::shapes`: Struct or Tuple or
/// Leaf, Wildcard, and the others it is.
uint16_t typeShapeBits(const Type *type);

class MatchTypeType final : public OtherType {
  public:
    static TypeCode typeCode();
    static MatchTypeType *create(
        Type *subject,
        std::span<const MatchTypeArm> arms,
        std::span<const std::pair<std::string, Type *>> pendingGuardBindings = {});

    Type *subject() const { return params_[0]; }
    size_t armCount() const { return shapes_.size(); }
    uint16_t armShapes(size_t arm) const { return shapes_[arm]; }
    std::span<Type *const> armIdentities(size_t arm) const;
    /// The patterns the case writes to take a pack apart; empty when it writes none.
    std::span<Type *const> armPackPatterns(size_t arm) const;
    /// The patterns the case writes to take an array type apart; empty when it writes none.
    std::span<Type *const> armArrayPatterns(size_t arm) const;
    Type *armResult(size_t arm) const;
    /// The guard the case writes, or null when it writes none.
    const std::shared_ptr<const TypeCaseGuard> &armGuard(size_t arm) const {
        static const std::shared_ptr<const TypeCaseGuard> kNone;
        return arm < guards_.size() ? guards_[arm] : kNone;
    }
    /// The names one case binds: what its patterns write for the subject's members -- the element
    /// type and the dimensions of an array pattern, the members and the rest of a pack pattern.
    /// They stand for what the case reached and are not type variables of the type the `match
    /// type` is part of (typevar.cpp `collectTypeVars`).
    void armBoundNames(size_t arm, std::vector<std::string> &names) const;

    /// The type arguments a case's guard was waiting for. A guard is written in the type function's
    /// own body, so the type it reads is a parameter of that function (`case _ when is_numeric::<T>`
    /// in `ShapeOf<T>`); the substitution that binds the parameter is the one that would run the
    /// predicate, and while no evaluator is installed it cannot. Those bindings are kept with the
    /// match instead of being dropped, and the build that runs the predicate asks the guard about
    /// them (typevar.cpp, `typeCaseGuardEvaluator`).
    std::span<const std::pair<std::string, Type *>> pendingGuardBindings() const {
        return std::span<const std::pair<std::string, Type *>>(
            pendingGuardBindings_.data(),
            pendingGuardBindings_.size());
    }
    /// This match with its params substituted and those bindings merged into the ones it waits for
    /// already: a name it has keeps its type, because a guard's answer is an answer about the
    /// subject, and two matches with one subject mean one thing by one name.
    MatchTypeType *withPendingGuardBindings(
        std::span<Type *const> params,
        std::span<const std::pair<std::string, Type *>> pending) const;

    bool typeArguments() const override { return true; }
    std::string toString() const override;
    std::string mangle() const override;
    Type *clone(bool deep = false) const override;
    bool equals(Type *type) const override;
    bool assignableFrom(Type *sourceType) const override;
    CastSafety castSafetyFrom(Type *sourceType) const override;
    OtherType *cloneWithParams(std::span<Type *const> params) const override;

  private:
    MatchTypeType(
        std::vector<uint16_t> shapes, std::vector<uint32_t> identityCounts,
        std::vector<uint32_t> packCounts, std::vector<uint32_t> arrayCounts, size_t paramCount,
        Type **params, std::vector<std::shared_ptr<const TypeCaseGuard>> guards = {},
        std::vector<std::pair<std::string, Type *>> pendingGuardBindings = {})
        : OtherType(typeCode(), paramCount, params), shapes_(std::move(shapes)),
          identityCounts_(std::move(identityCounts)), packCounts_(std::move(packCounts)),
          arrayCounts_(std::move(arrayCounts)), guards_(std::move(guards)),
          pendingGuardBindings_(std::move(pendingGuardBindings)) {}

    size_t firstParamOf(size_t arm) const;
    /// The arms, rebuilt from the params at their positions: the params a `cloneWithParams` was
    /// given, or this type's own.
    std::vector<MatchTypeArm> armsFrom(std::span<Type *const> params) const;

    std::vector<uint16_t> shapes_;
    std::vector<uint32_t> identityCounts_;
    std::vector<uint32_t> packCounts_;
    std::vector<uint32_t> arrayCounts_;
    /// One entry per case: the guard it writes, or null. Kept beside the params because a guard is
    /// not a type: it is the compiler's tree of a predicate (`TypeCaseGuard`).
    std::vector<std::shared_ptr<const TypeCaseGuard>> guards_;
    /// What a guard waits for when the match could not be settled where its parameters were bound.
    /// Not part of the match's identity: the subject says which application this is, and the
    /// bindings a guard reads come from that application (`create`).
    std::vector<std::pair<std::string, Type *>> pendingGuardBindings_;
};

/// Whether a type computed from types (a primitive application or a `match type`) occurs
/// anywhere in `type`.
bool containsTypeCall(Type *type);

/// Whether `type` holds a `match type` whose case a guard decides, so that selecting that case
/// waits for the predicate to run. The module tree builds types before the build that runs Camel
/// predicates, so a type that waits here is settled later by whoever installs the evaluator
/// (`typevar.h`, `typeCaseGuardEvaluator`); a caller must not report it as a type no case takes.
bool containsGuardedTypeMatch(Type *type);

/// Whether any `match type` built so far has carried a guard: a reader that would otherwise walk
/// every type it sees uses this to skip the walk in a program that writes no guard at all.
bool anyTypeCaseGuardBuilt();

/// What stays unevaluated in `type` although its arguments are concrete, said for a diagnostic:
/// `what` is the application (`ElementOf<int>`, `match type int {...}`) and `why` the reason
/// (`int is neither an array nor a set`, `no case selects int`). False when nothing of the kind is
/// left.
bool describeUnevaluatedTypeCall(Type *type, std::string &what, std::string &why);

} // namespace camel::core::type
