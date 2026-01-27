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
 * Created: Sep. 29, 2025
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "macro.h"

#include "compile/gir.h"
#include "core/context/context.h"
#include "core/operator.h"
#include "utils/assert.h"

using namespace GraphIR;

slot_t __cmp__(ArgsView &with, ArgsView &norm, Context &ctx) { return NullSlot; }
