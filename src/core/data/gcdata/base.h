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
 * Created: Nov. 07, 2025
 * Updated: Nov. 09, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "core/memory/gc.h"
#include "core/type/type.h"

using Int    = int32_t;
using Long   = int64_t;
using Float  = float;
using Double = double;
using Bool   = bool;
using Byte   = char;

struct DataLayout {
    uint16_t size;
    uint16_t align;
};

static_assert(sizeof(DataLayout) == 4, "DataLayout size must be 4 bytes");

DataLayout getLayoutOfType(TypeCode code);

inline size_t alignUp(size_t n, size_t align) { return (n + align - 1) & ~(align - 1); }
