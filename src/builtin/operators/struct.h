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
 * Created: Sep. 25, 2025
 * Updated: Oct. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"
#include "core/type/type.h"

OperatorReturnCode __len_str__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __len_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);

OperatorReturnCode __zip__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __head_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __tail_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __range__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __slice_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __concat_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __append_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __extend_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __contains_arr__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
