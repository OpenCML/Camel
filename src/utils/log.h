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
 * Created: Apr. 01, 2024
 * Updated: Oct. 19, 2024
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

#define info std::cout << _green("[info] ")
#define warn std::cout << _yellow("[warn] ")
#define error std::cout << _red("[error] ")
#define fatal std::cout << _red("[fatal] ")

extern size_t __depth__;

inline std::string repeatPattern(const std::string &pattern, int n) {
    std::string result;
    for (int i = 0; i < n; ++i) {
        result.append(pattern);
    }
    return result;
}

inline std::string pointerToHex(const void *ptr) {
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(sizeof(void *) * 2) << std::setfill('0') << reinterpret_cast<uintptr_t>(ptr);
    return ss.str();
}

// #define DEBUG_LEVEL -1

#define debug(level)                                                                                                   \
    if (level <= DEBUG_LEVEL)                                                                                          \
    std::cout << _blue("   [" #level "] ")

#define debug_u(level)                                                                                                 \
    if (level <= DEBUG_LEVEL)                                                                                          \
    std::cout

#define enter(target)                                                                                                  \
    do {                                                                                                               \
        if (DEBUG_LEVEL > 0) {                                                                                         \
            std::cout << repeatPattern("|   ", __depth__) << _blue("[enter] ") << target << std::endl;                 \
        }                                                                                                              \
        __depth__++;                                                                                                   \
    } while (false)

#define leave(target)                                                                                                  \
    do {                                                                                                               \
        __depth__--;                                                                                                   \
        if (DEBUG_LEVEL > 0) {                                                                                         \
            std::cout << repeatPattern("|   ", __depth__) << _green("[leave] ") << target << std::endl;                \
        }                                                                                                              \
    } while (false)

#ifdef NDEBUG

#define cml_assert(condition, message)                                                                                 \
    do {                                                                                                               \
    } while (false)

#else

#define cml_assert(condition, message)                                                                                 \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            std::cerr << "Assertion failed: (" #condition "), function " << __FUNCTION__ << ", file " << __FILE__      \
                      << ", line " << __LINE__ << ".\n"                                                                \
                      << "Message: " << message << std::endl;                                                          \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)

#endif
