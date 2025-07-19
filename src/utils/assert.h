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
 * Created: Jul. 19, 2025
 * Updated: Jul. 19, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <stacktrace>
#include <string>

namespace cml {

inline void handle_assert_failure(const std::string &expression, const std::string &message,
                                  const std::source_location location = std::source_location::current()) {
    std::cerr << "Camel Debug Assertion failed:\n"
              << "    Expression : " << expression << "\n"
              << "    Function   : " << location.function_name() << "\n"
              << "    Location   : " << location.file_name() << "(" << location.line() << ")\n"
              << "    Message    : " << message << "\n";

    std::cerr << "\nStack trace:\n";
    auto trace = std::stacktrace::current();
    constexpr size_t skip_front = 1;

    const size_t frame_count = trace.size();
    if (frame_count <= skip_front) {
        std::cerr << "(stack trace too short)\n";
    } else {
        for (size_t i = skip_front; i < frame_count; ++i) {
            std::cerr << "<" << i << "> " << trace[i] << "\n";
        }
    }

    std::abort();
}

} // namespace cml

#ifdef NDEBUG

#define ASSERT(condition, message) ((void)0)

#else

#define ASSERT1(cond)                                                                                                  \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            ::cml::handle_assert_failure(#cond, "");                                                                   \
        }                                                                                                              \
    } while (false)

#define ASSERT2(cond, msg)                                                                                             \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            ::cml::handle_assert_failure(#cond, msg);                                                                  \
        }                                                                                                              \
    } while (false)

#define GET_MACRO(_1, _2, NAME, ...) NAME
#define ASSERT(...) GET_MACRO(__VA_ARGS__, ASSERT2, ASSERT1)(__VA_ARGS__)

#endif