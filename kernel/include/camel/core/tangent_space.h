// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * "Does a value of this type have a tangent space, and is it the type itself?" -- the question a
 * transform asks about a type before it lifts anything, answered by the type and not by the writer
 * of a rule (design principles 9.9: the answer's shape comes from the node's type).
 *
 * Three answers are enough for a transform to decide:
 *
 *   - `None`: the value has no tangent -- an integer, a boolean, a `string`, a function. A
 *     transform that would have to carry a tangent for it refuses at the type, naming it.
 *   - `Self`: the tangent of a value is a value of the same type. Every floating scalar is here
 *     (`float`, `f32`, and the reduced-precision `f16` and `bf16`: a narrow float is a float, its
 *     tangent is float-shaped), and so is an array, a tensor or an aggregate whose parts are all
 *     `Self` -- the product of self-spaces is the shape itself.
 *   - `Other`: the tangent is a value of another type (`space`). A module that owns a type
 *     registers such a space with the derivative registry, and an aggregate of those answers with
 *     the aggregate of their tangent types.
 *
 * This is a *query*: it reads the type and the registry and changes nothing, so a pass, a
 * transform's type rule and a test may all ask it, and asking twice gives the same answer.
 */

#pragma once

#include "camel/core/type/base.h"

#include <cstdint>
#include <string>

namespace camel::core {

/// The three answers of `tangentSpaceOf`.
enum class TangentSpaceKind : uint8_t {
    /// The value has no tangent space: nothing may be differentiated with respect to it.
    None,
    /// The tangent space is the type itself (a float, an array of floats, a tuple of them).
    Self,
    /// The tangent space is another type, `TangentSpaceAnswer::space`.
    Other,
};

struct TangentSpaceAnswer {
    TangentSpaceKind kind = TangentSpaceKind::None;
    /// The space: the primal type itself for `Self`, the other type for `Other`, nullptr for
    /// `None`. Every answer that has a space answers with a type a caller can compare and print.
    type::Type *space = nullptr;

    bool hasSpace() const { return kind != TangentSpaceKind::None; }
    /// `none`, `self`, or `other=<type>`: the one word a reading prints.
    std::string describe() const;
};

/// The tangent space of values of `type`: `None`, the type itself (`Self`), or another type
/// (`Other`). A floating scalar is its own space; an array is its element's answer, its element
/// type replaced by that answer's space and its shape kept; a tuple or a struct is the product of
/// its members' answers (all `Self` is `Self`, one member with no space leaves the aggregate with
/// none); a type a module registered a space for answers with that space; everything else -- the
/// integers, the booleans, `string`, a function -- has none.
TangentSpaceAnswer tangentSpaceOf(const type::Type *type);

} // namespace camel::core