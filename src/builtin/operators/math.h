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
 * Updated: Feb. 17, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"
#include "core/type/type.h"

slot_t __abs__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __exp__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __round__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ceil__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __floor__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __bin__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __oct__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __hex__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __sqrt__(ArgsView &with, ArgsView &norm, Context &ctx);
