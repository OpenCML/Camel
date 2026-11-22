// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

/**
 * The `min`, `max` and `abs` overloads of the two reduced-precision floating types.
 * They are implemented in operators.cpp, next to the f32 and complex ones, and registered by the
 * numeric map in numeric.cpp — hence this small header rather than one of the two: either could
 * not name the other's declarations without including it.
 *
 * Each is the exact operation on the value the operand widens to, which is why no accumulator and
 * no accumulation width take part (design principles 7.3). The reasoning is written at the implementations in operators.cpp.
 */

#include "camel/core/operator.h"

namespace ctx = camel::core::context;

slot_t __math_min_bf__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_max_bf__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_min_f6__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_max_f6__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_abs_bf__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);
slot_t __math_abs_f6__(ArgsView &with, ArgsView &norm, ctx::Context &ctx);