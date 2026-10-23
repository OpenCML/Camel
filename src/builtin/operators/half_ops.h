// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The scalar operations of the two reduced-precision floating types (design principles 7.3):
 * `bf16` and `f16` are floating types with the arithmetic, the comparisons and the assignments
 * `f32` has, computed in f32 on values widened to f32 and rounded once back into the element
 * type -- the same one arithmetic path the tensor kernels use (modules/tensor/dtype.h). Writing
 * the operation is asking for that precision, so no numeric permission is involved.
 *
 * `bf` in a name is bf16 and `f6` is f16. The declarations here are used by ops.cpp (negation)
 * and by the tables in builtin.cpp; each is one instantiation of the two templates below.
 */

#pragma once

#include "camel/core/operator.h"

slot_t __builtin__neg_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__neg_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__add_bfbf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__sub_bfbf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__mul_bfbf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__div_bfbf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__pow_bfbf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__add_f6f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__sub_f6f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__mul_f6f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__div_f6f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__pow_f6f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__eq_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__ne_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__lt_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__le_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__gt_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__ge_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__eq_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__ne_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__lt_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__le_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__gt_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__ge_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

slot_t __builtin__assn_add_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_sub_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_mul_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_div_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_pow_bf__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_add_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_sub_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_mul_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_div_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __builtin__assn_pow_f6__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);