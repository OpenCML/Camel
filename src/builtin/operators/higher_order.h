// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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
