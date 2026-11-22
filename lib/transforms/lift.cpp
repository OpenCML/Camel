// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The parameter-list half of `Lift` (`lift.h` states the law; this is its definition).
 *
 * The law writes a parameter list as `F`'s parameters followed by each one's tangent, and the
 * tangent of a parameter with no tangent space is the empty slot `()` -- the slot keeps the two
 * halves lined up position for position, so a reader that walks them together never has to know
 * which values were left out. `liftedParametersOf` reads that back: which places are values, which
 * are the places a cotangent is answered for, and which are the empty slot.
 *
 * It lives in its own translation unit because it is one answer several readers ask (the cut asks
 * the same question of the same shape, `split.cpp#valuePortsOf`, and `transpose` asks it for its
 * ports, its participating places and its answer). A definition inside any one of those readers
 * would make that file the owner of the law instead of a reader of it.
 */

#include "lift.h"

#include "camel/core/tangent_space.h"
#include "camel/core/type/composite/tuple.h"

namespace camel::transforms {

namespace {

namespace type = camel::core::type;

/// `()`: the empty tuple. The one place a `LiftedPlace::Unit` comes from.
bool isEmptyTuple(const type::Type *place) {
    if (place == nullptr || place->code() != type::TypeCode::Tuple) {
        return false;
    }
    const auto *tuple = static_cast<const type::TupleType *>(place);
    return tuple != nullptr && tuple->size() == 0;
}

} // namespace

LiftedParameters liftedParametersOf(
    const camel::core::type::FunctionType *type, bool asLift) {
    LiftedParameters read;
    if (type == nullptr) {
        return read;
    }
    const size_t count = type->normTypesCount();
    read.places.assign(count, LiftedPlace::Tangent);
    if (count == 0) {
        // No parameter list at all, so there is nothing for the lift to write: a `() => r`
        // function's lift is `() => Lift<r>`, and every one of its (absent) places is trivially
        // not a value. Said rather than left to the loop below, which would divide by two.
        return read;
    }
    // **Is this list a lift?** Two facts together, and neither is enough alone. The caller's
    // `asLift` is the graph fact -- a forward transform wrote this signature (`liftedFrom`), so its
    // parameter list is `Lift<G>`'s -- and the law's own test is the shape fact: the second half is
    // the first half's tangents, position for position, with `()` where a parameter has no tangent
    // space. The shape test alone is not a reading: a cut's backward half for a two-parameter
    // function is `(f32, f32) => f32`, whose second half *is* the first half's tangent, and reading
    // that as a lift would answer one cotangent where the caller hands in one per parameter. So the
    // test confirms the law's shape for a signature the graph already says is lifted, and a caller
    // that has no graph fact passes false and gets the ordinary reading below.
    bool isLift = asLift && count % 2 == 0;
    const size_t half = isLift ? count / 2 : 0;
    for (size_t i = 0; isLift && i < half; ++i) {
        const camel::core::type::Type *value   = type->normTypeAt(i);
        const camel::core::type::Type *tangent = type->normTypeAt(half + i);
        if (value == nullptr || tangent == nullptr) {
            isLift = false;
            break;
        }
        const camel::core::TangentSpaceAnswer space = camel::core::tangentSpaceOf(value);
        if (space.hasSpace()) {
            isLift = tangent == space.space;
            continue;
        }
        // A parameter with no tangent space left the empty slot: the halves stay the same length,
        // and the slot is the answer this function gives, never an argument a caller fills.
        isLift = isEmptyTuple(tangent);
    }
    read.isLift = isLift;
    if (isLift) {
        for (size_t i = 0; i < half; ++i) {
            read.places[i]         = LiftedPlace::Value;
            read.places[half + i]  = isEmptyTuple(type->normTypeAt(half + i)) ? LiftedPlace::Unit
                                                                              : LiftedPlace::Tangent;
        }
        return read;
    }
    // Not a lift: the ordinary reading, and not a second law. A parameter whose type holds no
    // cotangent is a value the caller still passes (`isUnitType`'s neighbour -- an `int`, a
    // `bool`), and one whose type has a cotangent is a place the answer is a cotangent of.
    for (size_t i = 0; i < count; ++i) {
        const camel::core::type::Type *place = type->normTypeAt(i);
        if (isEmptyTuple(place)) {
            read.places[i] = LiftedPlace::Unit;
        } else if (place != nullptr && camel::core::tangentSpaceOf(place).hasSpace()) {
            read.places[i] = LiftedPlace::Tangent;
        } else {
            read.places[i] = LiftedPlace::Value;
        }
    }
    return read;
}

} // namespace camel::transforms