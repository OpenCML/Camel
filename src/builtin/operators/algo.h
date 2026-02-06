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
 * Created: Oct. 29, 2025
 * Updated: Feb. 06, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"

// void __argsort__(
//     ArgsView &with, ArgsView &norm, Context &ctx);
// void __sort__(
//     ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __insert_sort__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __insert_sort_inplace__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __quick_sort__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __quick_sort_inplace__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __merge_sort__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __merge_sort_inplace__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __merge_sorted_arrays__(ArgsView &with, ArgsView &norm, Context &ctx);
