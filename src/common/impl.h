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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Jul. 15, 2025
 * Updated: Jul. 15, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>

enum class ImplMark { Inner, Outer, Graph };

inline std::string to_string(ImplMark mark) {
    switch (mark) {
    case ImplMark::Inner:
        return "inner";
    case ImplMark::Outer:
        return "outer";
    case ImplMark::Graph:
        return "graph";
    default:
        return "unknown";
    }
}