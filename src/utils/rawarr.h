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
 * Created: Oct. 25, 2025
 * Updated: Oct. 25, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "assert.h"

#include <cstdint>

template <typename T> struct RawArray {
    T *data;
    size_t size;

    T &operator[](size_t idx) {
        ASSERT(idx < size, std::format("Array index out of range: {}, size: {}", idx, size));
        return data[idx];
    }

    const T &operator[](size_t idx) const {
        ASSERT(idx < size, std::format("Array index out of range: {}, size: {}", idx, size));
        return data[idx];
    }
};