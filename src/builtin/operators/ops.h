// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/operator.h"
#include "camel/core/rtdata/equality.h"
#include "camel/core/type.h"

slot_t __builtin__var_new__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__var_new_empty__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__var_after__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__var_get__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__var_peek__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__var_take__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_owned__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

/// `:op/keep(x, v)`: the value `v`, with `x` as an input whose value no result takes, so that the
/// node `x` names stays in the data-use set the liveness function computes (design principles
/// 3.3). A pass that removes the last use of a node that must still run -- an effect, or a
/// computation that may fault -- routes that node here instead of dropping it, and `v` stands
/// where the node's value did. The body is a pass-through and stands beside its declaration; the
/// table declares the operator conservatively (an effect, may fault; operator_table.h), so no
/// pass drops it, merges it, moves it or computes it early.
inline slot_t __keep__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    (void)with;
    (void)ctx;
    return norm.slot(1);
}

slot_t __builtin__assn_add_i__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_add_l__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_add_f__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_add_d__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_add_s__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__assn_sub_i__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_sub_l__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_sub_f__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_sub_d__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__assn_mul_i__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_mul_l__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_mul_f__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_mul_d__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__assn_div_i__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_div_l__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_div_f__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_div_d__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__assn_mod_i__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_mod_l__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_pow_i__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_pow_l__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_pow_f__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_pow_d__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__assn_and__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_or__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

template <typename T>
slot_t __builtin__lt__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    T lhs = norm.get<T>(0);
    T rhs = norm.get<T>(1);
    return toSlot(Bool(lhs < rhs));
}

template <typename T>
slot_t __builtin__le__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    T lhs = norm.get<T>(0);
    T rhs = norm.get<T>(1);
    return toSlot(Bool(lhs <= rhs));
}

template <typename T>
slot_t __builtin__gt__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    T lhs = norm.get<T>(0);
    T rhs = norm.get<T>(1);
    return toSlot(Bool(lhs > rhs));
}

template <typename T>
slot_t __builtin__ge__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx) {
    T lhs = norm.get<T>(0);
    T rhs = norm.get<T>(1);
    return toSlot(Bool(lhs >= rhs));
}

slot_t __builtin__lt_s__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__le_s__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__gt_s__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__ge_s__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__eq__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__ne__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__and__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__or__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__add_ii__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__add_ll__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__add_ff__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__add_dd__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__add_ss__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__add_cc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__add_zz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__sub_ii__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__sub_ll__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__sub_ff__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__sub_dd__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__sub_cc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__sub_zz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__mul_cc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__mul_zz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__div_cc__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__div_zz__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__neg_c__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__neg_z__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__assn_add_c__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_add_z__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_sub_c__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_sub_z__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_mul_c__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_mul_z__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_div_c__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_div_z__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__pow_c__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__pow_z__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_pow_c__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_pow_z__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__mul__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__div__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__mod__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__pow__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__idx__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__not__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__neg__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__inv__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
