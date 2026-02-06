/**
 * Copyright (c) 2024 the OpenCML Organization
 * Camel is licensed under the MIT license.
 * You can use this software according to the terms and conditions of the
 * MIT license. You may obtain a copy of the MIT license at:
 * [https://opensource.org/license/mit]
 *
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY
 * KIND, EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
 * NON-INFRINGEMENT, MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 *
 * See the the MIT license for more details.
 *
 * Author: Zhenjie Wei
 * Created: Oct. 06, 2024
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "compile/gir.h"
#include "core/context/context.h"
#include "core/operator.h"
#include "src/builtin/datas/tensor.h"

// Tensor static factory methods
slot_t __tensor_eye__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_diag__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_linspace__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_arange__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_zeros__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_ones__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_random__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_randn__(ArgsView &with, ArgsView &norm, Context &ctx);

// Tensor arithmetic operations
slot_t __tensor_add__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_subtract__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_multiply__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_matmul__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_divide__(ArgsView &with, ArgsView &norm, Context &ctx);

// Tensor shape transformation operations
slot_t __tensor_shape__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_reshape__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_transpose__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_flatten__(ArgsView &with, ArgsView &norm, Context &ctx);

// Tensor combination operations
slot_t __tensor_concat__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_stack__(ArgsView &with, ArgsView &norm, Context &ctx);

// Tensor statistical operations
slot_t __tensor_sum__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_mean__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_min__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_max__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_argmin__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_argmax__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_std__(ArgsView &with, ArgsView &norm, Context &ctx);

// Tensor norm calculations
slot_t __tensor_norm_l1__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_norm_l2__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_norm_squared_l2__(ArgsView &with, ArgsView &norm, Context &ctx);

// Tensor mathematical functions
slot_t __tensor_sin__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_cos__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_exp__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_log__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_sqrt__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_pow__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_matpow__(ArgsView &with, ArgsView &norm, Context &ctx);

// Tensor hyperbolic functions
slot_t __tensor_sinh__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_cosh__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __tensor_tanh__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __tensor_show__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __to_float__(ArgsView &with, ArgsView &norm, Context &ctx);
