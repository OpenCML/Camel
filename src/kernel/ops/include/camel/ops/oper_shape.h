// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The shape of one of the four operations a node can perform -- a conversion, a copy, a
 * construction and a member read -- as a **property of the operator**, which is where it is stated
 * and read (D-1123-34).
 *
 * The vocabulary is the kernel's, not a back end's: `Record::shape` holds it, so that a reader of a
 * graph asks the operator's record what the operation does instead of comparing the letters of a
 * URI, and no kernel file has to include a standard-library header to ask (the header this enum
 * used to live in, `camel/execute/shaped_operators.h`, is the library's, and the kernel does not
 * depend on the library).
 *
 * `None` is both "not one of the four" and the answer for an operator nobody declared anything
 * about: the shape is a positive statement, written where the operator is registered, and an
 * operator that did not state it is not one of the four.
 *
 * What a back end does with one of the four, and the fast paths that run them, stay in the library
 * (`camel/execute/shaped_operators.h`): a header that runs a frame belongs to whoever runs graphs,
 * and the kernel only names the shape.
 */

#pragma once

#include <cstdint>

namespace camel::ops {

/// What one of the four operators does, which is all a back end has to know to run it.
enum class OperShape : uint8_t {
    None = 0,     // not one of the four: the node kind says what it is to the back end
    Conversion,   // CAST: its one ordinary input converted to the node's type
    Clone,        // COPY: a value of its own made from its input
    Construction, // FILL: an aggregate made from a template and its fields
    Access,       // ACCS: a member or an element read out of its input
};

/// The operator's own name, which is what a back end's dump shows for it ("CAST", "COPY", "FILL",
/// "ACCS"). Empty for None.
constexpr const char *shapeName(OperShape shape) {
    switch (shape) {
    case OperShape::Conversion:
        return "CAST";
    case OperShape::Clone:
        return "COPY";
    case OperShape::Construction:
        return "FILL";
    case OperShape::Access:
        return "ACCS";
    case OperShape::None:
        break;
    }
    return "";
}

} // namespace camel::ops