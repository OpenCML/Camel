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
 * Updated: Sep. 29, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"
#include "core/type/type.h"

OperatorReturnCode __builtin__assn__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__assn_add__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__assn_sub__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__assn_mul__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__assn_div__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__assn_mod__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__assn_mat__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__assn_exp__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__assn_and__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__assn_or__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

OperatorReturnCode __builtin__or__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__and__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__eq__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__neq__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__strict_eq__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__strict_neq__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__lt__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__le__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__gt__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__ge__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

OperatorReturnCode __builtin__add__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__sub__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__mul__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__div__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__mod__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__mat__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__pow__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __builtin__idx__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
