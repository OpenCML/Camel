// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * Which subscript form of the language an operator is the node of, as a **property of the
 * operator**, stated where it is registered and read from the operator's record.
 *
 * The language has three of these forms, and a reader that meets one of their nodes has to tell
 * them apart to know how many ordinary operands the node carries and which of them is the step
 * (design principles 5.4, "subscripts and slices"): `t[i]` reads an element at a position,
 * `t[a..b:s]` is a view of one axis, and `range(a, b, s)` is the integers of a step. `range` is
 * not written with brackets, and it is the same family: it is the form that *carries* a step, so
 * a reader deciding what a constant step of zero means asks the same question of it.
 *
 * The three deliberately have no compile-time id (camel/core/operator_table.h: only the operators
 * the compiler itself *makes* have one). An id would say *which operator* it is, and a table of
 * ids for these would be the special case by name that the id table replaces, kept in a second
 * place: what the reader wants is which form it is -- a fact about what the operator does, which
 * belongs on the operator's record beside its shape (camel/ops/oper_shape.h).
 *
 * `None` is both "not one of these" and the answer for an operator nobody stated anything about:
 * the form is a positive statement, written where the operator is registered, and an operator
 * that did not state it is not one of them.
 */

#pragma once

#include <cstdint>
#include <string_view>

namespace camel::ops {

enum class SubscriptForm : uint8_t {
    None = 0, // not one of these: whatever the node is, it is not a subscript form
    Index,    // `t[i]`, `m[:, 0]`: a position read out of an operand (the index of a `[]` call)
    Slice,    // `t[a..b:s]`: a view of one axis of an operand (`:op/slice_*`)
    Range,    // `range(a, b, s)`: the integers of a step, as an array (`:struct/range`)
};

/// The operator's own word for the form ("INDEX", "SLICE", "RANGE"); empty for None.
constexpr const char *subscriptFormName(SubscriptForm form) {
    switch (form) {
    case SubscriptForm::Index:
        return "INDEX";
    case SubscriptForm::Slice:
        return "SLICE";
    case SubscriptForm::Range:
        return "RANGE";
    case SubscriptForm::None:
        break;
    }
    return "";
}

} // namespace camel::ops