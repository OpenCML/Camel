// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * `Lift<T>` -- the type a value of type `T` has after a forward transform, and **the one place**
 * that shape is computed. A function value lifts by type, so `Lift<A ⇒ B>` is `Lift<A> ⇒ Lift<B>`.
 *
 *     Lift<T>        = (T, Tangent<T>)        for a `T` that has a tangent space -- unchanged
 *     Lift<A => B>   = Lift<A> => Lift<B>     a function lifts to its lifted self
 *     Lift<(T0…Tn)>  = (Lift<T0> … Lift<Tn>)  a result that holds a function lifts by component
 *
 * The third case is the shape that had no answer before. `tangentSpaceOf` answers `None` for a pair
 * with a function member (a function has none, and an aggregate with one member without a space has
 * none either), so a forward transform had nothing to say about `split`'s forward half handing out
 * `(float, (float) => float)`, and every reader of that shape refused. **No tangent is invented for
 * the function**: a function is not a vector space and has no tangent vector (design principles 9.9),
 * and a transform whose implementation *did* hand a function a tangent vector would have implemented
 * the wrong thing. What lifts is the *value*: a value
 * component becomes its own pair, and a function component becomes that function's lift, which is
 * its lifted signature -- this same law one level down.
 *
 * The law has two readers: `split`'s forward half hands out a lifted shape such as
 * `(float, (float) => float)`, and `transpose` recognizes the lifted function components the same
 * way. The two readers are the transform that writes the lifted graph (`jvp.cpp`, which also
 * *defines* this function), and the cut that reads a lifted signature (`split.cpp`). Both ask this
 * one question, so the type a lifted node is written with and the shape a cut is willing to cut
 * cannot drift apart. Nothing about `Lift<T>` is restated at a call site: a site that needs to know
 * whether a member is a function asks this law, not its own kind test.
 *
 * The law writes a parameter list as well as an answer: `Lift<F>`'s parameters are `F`'s, each one
 * followed by its tangent. `liftedParametersOf` is that half of the law read backwards, and it is
 * the one entry a reader asks to know which places are values and which are the places a cotangent
 * is answered for -- the question `transpose` decides its ports, its participating places and its
 * answer from. The cut asks the same question of the same shape (`split.cpp#valuePortsOf`), so the
 * value half `split` hands out and the value places `transpose` fills are one reading of one type.
 */

#pragma once

#include "camel/core/type/composite/func.h"
#include "camel/core/type/composite/tuple.h"

#include <algorithm>
#include <cstddef>
#include <vector>

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
    /// for it (design principles 9.9).
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

/// What one place of a parameter list is, when the list is read as `Lift`'s: the lift writes a
/// parameter that has a tangent space as **two** places -- the value and its tangent -- and a
/// parameter that has none as its value and the empty slot `()`.
enum class LiftedPlace {
    /// A place the caller **fills with a value**: the value half of a lift, and (in a signature
    /// that is not a lift) a parameter whose type holds no cotangent, such as an `int`.
    Value,
    /// A place **a cotangent is answered for**: the tangent half of a lift, and (in a signature
    /// that is not a lift) every parameter whose type has a tangent space.
    Tangent,
    /// `()`: the empty slot a parameter with no tangent space leaves in the tangent half. It is an
    /// answer with nothing in it, never a port -- nobody can pass a value where there is none.
    Unit,
};

/// One parameter list, read the way `Lift` writes it: which places are values and which are the
/// places a cotangent is answered for. This is `liftSignature` read backwards, and it is **the one
/// reader** of that split -- `transpose`'s participating places, its value ports, and its answer
/// are all this answer, so the ports a member takes and the places it answers a cotangent for
/// cannot be decided by two different tests.
///
///     Lift<G>'s places = (each parameter's value) then (each one's Tangent, `()` where none)
///
/// so a parameter list **is** a lift exactly when its second half is the first half's tangents
/// position for position -- the test the cut reads as well (`valuePortsOf`), so a type a transform
/// writes and a reading of that type cannot drift apart. Which of the two readings applies is not
/// decided here: the law's shape is one thing, and a graph's own record of having been lifted is
/// another (`asLift` below), because the same shape is also what a plain two-parameter cut's backward
/// half has.
///
/// A list that is not a lift is read the ordinary way, and that case is not a second law: a
/// parameter whose type holds no cotangent is a value the caller passes and no answer is given for
/// it, and one whose type has a cotangent is answered for. `isLift` says which of the two readings
/// the answer came from, because a caller that has a *graph* in hand may need to say so as well;
/// nothing decides the places itself.
struct LiftedParameters {
    /// Whether the parameter list is `Lift<G>`'s as the law writes it.
    bool isLift = false;
    /// One entry per parameter, in order. Empty only when the type is null.
    std::vector<LiftedPlace> places;

    /// Whether a cotangent is answered for the parameter at `at`.
    bool answers(size_t at) const {
        return at < places.size() && places[at] == LiftedPlace::Tangent;
    }
    /// Whether the parameter at `at` is a value the caller fills.
    bool fills(size_t at) const {
        return at < places.size() && places[at] == LiftedPlace::Value;
    }
    /// Whether any place of this list is a value, which is what "not every place is a tangent"
    /// means to a reader that has to know whether it is transposing the whole list.
    bool hasValues() const {
        return std::find(places.begin(), places.end(), LiftedPlace::Value) != places.end();
    }
};

/// The places of `type`'s parameter list, read by the law above. `asLift` is the caller's fact about
/// the **graph** the signature belongs to: a forward transform wrote it (`GCGraph::liftedFrom`), so
/// its parameter list is `Lift<G>`'s and the first half is the values. No type says that on its own,
/// and the two readings overlap on one shape -- a cut's backward half for a two-parameter function is
/// `(f32, f32) => f32` and a two-parameter function's *lifted* parameter list is `(f32, f32)` as
/// well -- so a reader that guessed from the type would read an ordinary backward half as a lift and
/// answer one cotangent where the caller hands in one cotangent per parameter. A caller without that
/// fact passes false, and the ordinary reading is the answer.
LiftedParameters liftedParametersOf(
    const camel::core::type::FunctionType *type, bool asLift);

} // namespace camel::transforms