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
 * Updated: Oct. 15, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "src/builtin/datas/tensor.h"
#include "core/operator.h"
#include "compile/gir.h"
#include "core/context/frame.h"
#include "core/context/context.h"

// Tensor static factory methods
OperatorReturnCode __eye__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __diag__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __linspace__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __arange__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __zeros__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __ones__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

// Tensor arithmetic operations
OperatorReturnCode __tensor_add__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_subtract__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_multiply__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_divide__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

// Tensor shape transformation operations
OperatorReturnCode __shape__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_reshape__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_transpose__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_flatten__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

// Tensor combination operations
OperatorReturnCode __tensor_concat__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_stack__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

// Tensor statistical operations
OperatorReturnCode __tensor_sum__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_mean__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_min__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_max__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_argmin__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_argmax__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_std__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

// Tensor norm calculations
OperatorReturnCode __tensor_norm_l1__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_norm_l2__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_norm_squared_l2__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

// Tensor mathematical functions
OperatorReturnCode __tensor_sin__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_cos__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_exp__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_log__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_sqrt__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

// Tensor hyperbolic functions
OperatorReturnCode __tensor_sinh__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_cosh__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tensor_tanh__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);