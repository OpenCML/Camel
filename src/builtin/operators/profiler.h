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
 * Created: Oct. 1, 2025
 * Updated: Oct. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/context/frame.h"
#include "core/operator.h"

OperatorReturnCode __profiler_begin__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __profiler_end__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __profiler_instant__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
OperatorReturnCode __profiler_enable__(GraphIR::node_ptr_t &self, Frame &frame, Context &ctx);
