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
 * Updated: Jan. 27, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/operator.h"

slot_t __itoi__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ltoi__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ftoi__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __dtoi__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __stoi__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __itol__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ltol__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ftol__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __dtol__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __stol__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __itof__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ltof__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ftof__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __dtof__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __stof__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __itod__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ltod__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ftod__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __dtod__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __stod__(ArgsView &with, ArgsView &norm, Context &ctx);

slot_t __itos__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ltos__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __ftos__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __dtos__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __stos__(ArgsView &with, ArgsView &norm, Context &ctx);
slot_t __atos__(ArgsView &with, ArgsView &norm, Context &ctx);
