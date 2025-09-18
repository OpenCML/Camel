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
 * Created: Jul. 29, 2025
 * Updated: Jul. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"
#include "core/type/type.h"

EvalResultCode __builtin__assn__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__assn_add__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__assn_sub__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__assn_mul__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__assn_div__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__assn_mod__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__assn_mat__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__assn_exp__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__assn_and__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__assn_or__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);

EvalResultCode __builtin__or__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__and__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__eq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__neq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__strict_eq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__strict_neq__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__lt__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__le__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__gt__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__ge__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);

EvalResultCode __builtin__add__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__sub__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__mul__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__div__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__mod__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__mat__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__exp__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
EvalResultCode __builtin__idx__(GIR::node_ptr_t &self, Frame &frame, Context &ctx);
