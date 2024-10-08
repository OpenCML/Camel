/**
 * Copyright (c) 2024 Beijing Jiaotong University
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
 * Created: Apr. 1, 2024
 * Updated: Oct. 08, 2024
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <iostream>

#define _red(x) "\033[31m" << x << "\033[0m"
#define _blue(x) "\033[34m" << x << "\033[0m"
#define _green(x) "\033[32m" << x << "\033[0m"
#define _yellow(x) "\033[33m" << x << "\033[0m"

#define info std::cout << _green("[info] ")
#define warn std::cout << _yellow("[warn] ")
#define error std::cout << _red("[error] ")
#define fatal std::cout << _red("[fatal] ")

#define DEBUG_LEVEL -1

#define debug(level)                                                                                                   \
    if (level <= DEBUG_LEVEL)                                                                                          \
    std::cout << _blue("   [" #level "] ")

#define debug_u(level)                                                                                                 \
    if (level <= DEBUG_LEVEL)                                                                                          \
    std::cout

#ifdef NDEBUG

#define assert(condition, message)                                                                                     \
    do {                                                                                                               \
    } while (false)

#else

#define assert(condition, message)                                                                                     \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "Assertion failed: (" #condition "), function " << __FUNCTION__ << ", file " << __FILE__      \
                      << ", line " << __LINE__ << ".\n"                                                                \
                      << "Message: " << message << std::endl;                                                          \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

#endif
