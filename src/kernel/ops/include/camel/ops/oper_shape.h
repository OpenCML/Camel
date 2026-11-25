// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The shape of one of the operations a node can perform -- a conversion, an entering, a copy, a
 * construction and a member read -- as a **property of the operator**, which is where it is stated
 * and read.
 *
 * An entering is the step the compiler writes to make two types meet: a value taking `T?`, a failed
 * value taking the `!` of its position, a rank-zero array an open rank holds. It takes one value as
 * its own type like a conversion and cannot fail, which is why a back end lowers the two the same
 * way -- but it is not a conversion, and the shape says so: a conversion is what a *program* asked
 * for, interpreted against the safety table of the pair, and a fault the program asked for is that
 * table's answer. An entering asks no table (design principles 5.1, "entering is not a conversion").
 * A reader that asks what a node does with its value and has no reason to tell the two apart asks
 * `convertsValue`; one that has a reason writes both shapes out.
 *
 * The vocabulary is the kernel's, not a back end's: `Record::shape` holds it, so that a reader of a
 * graph asks the operator's record what the operation does instead of comparing the letters of a
 * URI, and no kernel file has to include a standard-library header to ask (that header,
 * `camel/execute/shaped_operators.h`, is the library's, and the kernel does not depend on the
 * library).
 *
 * `None` is both "not one of these" and the answer for an operator nobody declared anything about:
 * the shape is a positive statement, written where the operator is registered, and an operator that
 * did not state it is not one of them.
 *
 * What a back end does with a shape, and the fast paths that run them, stay in the library
 * (`camel/execute/shaped_operators.h`): a header that runs a frame belongs to whoever runs graphs,
 * and the kernel only names the shape.
 */

#pragma once

#include <cstdint>

namespace camel::ops {

/// What one of the operators does, which is all a back end has to know to run it. A shape added
/// later goes at the end: `Record::shape` is a field of the published record, and a value once
/// written to one is never moved.
enum class OperShape : uint8_t {
    None = 0,     // not one of these: the node kind says what it is to the back end
    Conversion,   // CAST: its one ordinary input converted to the node's type, checked
    Clone,        // COPY: a value of its own made from its input
    Construction, // FILL: an aggregate made from a template and its fields
    Access,       // ACCS: a member or an element read out of its input
    Entering,     // ENTER: its one ordinary input taken as the node's type, never checked
    Refining,     // SHAPECHK: its one ordinary input taken as the node's type, its extents checked
};

/// Whether the operator takes its one ordinary operand and hands back a value of the node's own
/// type: a cast, an entering and a refinement all do, and that is the whole of what a lowering
/// needs to know -- one operand in, one value out, named by the node's type. It says nothing about
/// whether the operator can fail or what it checks; that is the record's `panic` and its fault
/// table, and each of the three states its own (design principles 3.9, 5.1).
///
/// A reader that asks only "how does this node treat its value" asks this. One that must tell the
/// three apart writes all three shapes out, because the shapes state three different things: a cast
/// is interpreted against the safety table of its pair, an entering cannot fail, and a refinement
/// checks the shape of the extent its call carries.
constexpr bool takesItsOperandAsItsType(OperShape shape) {
    return shape == OperShape::Conversion || shape == OperShape::Entering ||
           shape == OperShape::Refining;
}

/// The operator's own name, which is what a back end's dump shows for it ("CAST", "ENTER",
/// "SHAPECHK", "COPY", "FILL", "ACCS"). Empty for None. `SHAPECHK` names what a refinement is --
/// a check of the value's shape -- rather than a spelling of the conversion it is not.
constexpr const char *shapeName(OperShape shape) {
    switch (shape) {
    case OperShape::Conversion:
        return "CAST";
    case OperShape::Entering:
        return "ENTER";
    case OperShape::Refining:
        return "SHAPECHK";
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