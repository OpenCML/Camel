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
 * Created: Oct. 11, 2026
 * Updated: Oct. 11, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * Built-in operators over the tagged forms `T?` and `T!` (design principles 3.8;
 * docs/technical/29_nullable_and_fallible_values.md).
 *
 * The first group is what `??`, `!!`, `?->`, `throw` and the `match` patterns are lowered
 * to: reading the tag, taking the value or the error out. Their names are reserved
 * identifiers, so a program cannot call them. The second group is ordinary library
 * operators that can fail and therefore return a `T!`.
 */

#pragma once

#include "camel/core/operator.h"
#include "camel/core/type.h"

#include <vector>

// Lowering targets.
slot_t __is_null__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __null_value__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __is_error__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __error_value__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __error_info__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __throw__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
// The arm of a `match` without `case _` that is taken when no case matches: a fault.

// Library.
slot_t __error_new__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __parse_int__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);
slot_t __parse_float__(ArgsView &with, ArgsView &norm, camel::core::context::Context &ctx);

/// The operator groups of this file, exported by the globals module beside its own.
const std::vector<oper_group_ptr_t> &getTaggedOperatorGroups();
