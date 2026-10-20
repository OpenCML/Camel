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
 * Author: Camel Contributors
 * Created: Oct. 19, 2026
 * Updated: Oct. 19, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The higher-order array operators as ordinary native operators (design principles 3.6): each
 * calls its function value through the function-call service (camel/execute/function_call.h),
 * element after element, in order. This is the one implementation of what they mean; the
 * schedulers that have a faster way to run a shape of call (OperatorSemantics::elementCalls)
 * use it, and the macro evaluator, which has no such way, runs these.
 */

#pragma once

#include "camel/core/operator.h"

slot_t __map_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __filter_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __reduce_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __foreach_arr__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
