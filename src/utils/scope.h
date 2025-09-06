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
 * Created: Apr. 01, 2024
 * Updated: Oct. 22, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iomanip>
#include <iostream>
#include <sstream>

#define _red(x) "\033[31m" << x << "\033[0m"
#define _blue(x) "\033[34m" << x << "\033[0m"
#define _green(x) "\033[32m" << x << "\033[0m"
#define _yellow(x) "\033[33m" << x << "\033[0m"

extern size_t __depth__;

inline std::string rainbowPattern(int depth) {
    static const std::string colors[] = {
        "\033[31m", // Red
        "\033[33m", // Yellow
        "\033[32m", // Green
        "\033[36m", // Cyan
        "\033[34m", // Blue
        "\033[35m", // Magenta
    };
    const int colorCount = sizeof(colors) / sizeof(colors[0]);

    std::stringstream ss;
    for (int i = 0; i < depth; ++i) {
        ss << colors[i % colorCount] << "| " << "\033[0m";
    }
    return ss.str();
}

// #define DEBUG_LEVEL -1

#define ENTER(target)                                                                              \
    do {                                                                                           \
        if (DEBUG_LEVEL > 0) {                                                                     \
            std::cout << rainbowPattern(__depth__) << _blue("[enter] ") << target << std::endl;    \
        }                                                                                          \
        __depth__++;                                                                               \
    } while (false)

#define LEAVE(target)                                                                              \
    do {                                                                                           \
        __depth__--;                                                                               \
        if (DEBUG_LEVEL > 0) {                                                                     \
            std::cout << rainbowPattern(__depth__) << _green("[leave] ") << target << std::endl;   \
        }                                                                                          \
    } while (false)
