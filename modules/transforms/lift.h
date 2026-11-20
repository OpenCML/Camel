// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `Lift<T>` -- the type a value of type `T` has after a forward transform (the author's ruling of
 * 2026-11-20 16:00: 函数值按类型提升, `Lift<A ⇒ B>` 是 `Lift<A> ⇒ Lift<B>`), and **the one place**
 * that shape is computed.
 *
 *     Lift<T>        = (T, Tangent<T>)        for a `T` that has a tangent space -- unchanged
 *     Lift<A => B>   = Lift<A> => Lift<B>     a function lifts to its lifted self
 *     Lift<(T0…Tn)>  = (Lift<T0> … Lift<Tn>)  a result that holds a function lifts by component
 *
 * The third case is the shape that had no answer before. `tangentSpaceOf` answers `None` for a pair
 * with a function member (a function has none, and an aggregate with one member without a space has
 * none either), so a forward transform had nothing to say about `split`'s forward half handing out
 * `(float, (float) => float)`, and every reader of that shape refused. **No tangent is invented for
 * the function**: a function is not a vector space and has no tangent vector (design principles 9.9,
 * which the ruling leaves standing), and a transform whose implementation *did* hand a function a
 * tangent vector would have implemented the wrong thing. What lifts is the *value*: a value
 * component becomes its own pair, and a function component becomes that function's lift, which is
 * its lifted signature -- this same law one level down.
 *
 * The law has two readers by the ruling itself ("`split`'s forward half handing out
 * `(float, (float) => float)` thus has a lifted shape ... `transpose` recognizes the lifted function
 * components the same way"): the transform that writes the lifted graph (`jvp.cpp`, which also
 * *defines* this function), and the cut that reads a lifted signature (`split.cpp`). Both ask this
 * one question, so the type a lifted node is written with and the shape a cut is willing to cut
 * cannot drift apart. Nothing about `Lift<T>` is restated at a call site: a site that needs to know
 * whether a member is a function asks this law, not its own kind test.
 */

#pragma once

#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"

namespace camel::transforms {

struct LiftedShape {
    /// `Lift<T>`, or null when the law answers nothing for `T` (an `int`, a `string`, a `bool`).
    camel::core::type::Type *type = nullptr;
    /// Whether `Lift<T>` is the pair `(T, Tangent<T>)` -- the shape a forward transform carries as
    /// two halves (`valueOf`/`tangentOf`). False for a lifted function and for a tuple that lifts by
    /// component: their lifted value *is* the value, and there is no separate tangent half to carry.
    bool pair = false;
};

/// `Lift<T>`; the definition lives in `jvp.cpp`, beside the signature law that its function case
/// answers with (`liftSignature`), so the two are one reader of the type system and not two.
LiftedShape liftedShapeOf(camel::core::type::Type *type);

/// One member of a lifted answer, **read back** into the source member it came from. This is `Lift`'s
/// inverse on the answer side, and it is what a cut needs: `split` holds `jvp(F)`'s type, whose exit
/// is `Lift<R>`, and it does **not** hold `R`, so "is this member a value or a tangent?" cannot be
/// asked of `R`. It can be asked of the member, because `Lift` writes each member one of two ways:
///
///     Lift<ti> = (ti, Tangent<ti>)   a member with a tangent space -- a pair, both halves named
///     Lift<F>  = liftSignature(F)    a function member -- the lifted function itself, no tangent
///
/// so for a non-pair `Lift<R>` each member `m` is read as:
///
///   - `m` a function            -> a function component: the forward side keeps `m` as it stands (a
///                                  lifted function is a value), the backward side takes nothing;
///   - `m` a two-member tuple `(a, b)` with `b == Tangent<a>` -> a value component: the forward side
///                                  keeps `a`, the backward side takes `b`;
///   - anything else             -> the law does not answer, and neither does this.
///
/// **Why this is unambiguous** (the thing that makes it a reading and not a guess): a member that is
/// genuinely a two-tuple in `R` does not appear as `(x, y)` inside `Lift<R>` -- it appears
/// **doubled**, as `((x, y), (x, y))` = `Lift<(x, y)>`, so it reads as the single source member
/// `(x, y)` and not as two. The pair form and the doubled form are different types, so a member is
/// decoded one way only.
struct LiftedMember {
    /// What the **forward** side answers for this member: `ti` for a value component, the lifted
    /// function itself for a function component, null when the member is not readable.
    camel::core::type::Type *forward = nullptr;
    /// What the **backward** side answers for this member: `Tangent<ti>` for a value component and
    /// null for a function component -- a function has no tangent space, and no tangent is invented
    /// for it (design principles 9.9; the ruling of 2026-11-20 16:00 leaves it standing).
    camel::core::type::Type *tangent = nullptr;
    /// Whether the member is a value component (a pair) rather than a function component.
    bool value = false;
};

/// Reads one member of a non-pair `Lift<R>` back into its source member; `forward == nullptr` when
/// the law does not answer for that member.
LiftedMember liftedMemberOf(camel::core::type::Type *member);

/// The type of the expression `jvp(f)` for a function of this type: `(f: F) => Lift<F>`. Named once,
/// here, because a lifted graph and a cut both write a node for `jvp(lin)` -- a cut's value half
/// hands a call's linear half on **lifted** -- and a second reconstruction of the operator's
/// signature at either site would be a second answer to the same question.
camel::core::type::FunctionType *liftedOperatorType(camel::core::type::FunctionType *function);

} // namespace camel::transforms