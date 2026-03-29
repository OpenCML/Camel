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
 * Updated: Mar. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <crtdbg.h>
#include <stacktrace>
#define HAS_STACKTRACE 1
inline void disable_windows_abort_dialogs() {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, 0);
    _CrtSetReportMode(_CRT_ERROR, 0);
    _CrtSetReportMode(_CRT_WARN, 0);
}
#else
#define HAS_STACKTRACE 0
#endif

namespace cml {

/** Thrown when ASSERT fails and a custom assert handler is set (e.g. by debugger). */
class AssertionFailure : public std::runtime_error {
  public:
    explicit AssertionFailure(
        const std::string &expression, const std::string &suggestion,
        const std::source_location &location = std::source_location::current())
        : std::runtime_error(buildMessage(expression, suggestion, location)),
          expression_(expression), suggestion_(suggestion), location_(location) {}

    const std::string &expression() const { return expression_; }
    const std::string &suggestion() const { return suggestion_; }
    const std::source_location &location() const { return location_; }

  private:
    static std::string buildMessage(
        const std::string &expression, const std::string &suggestion,
        const std::source_location &location) {
        std::string msg = "Assertion failed: " + expression;
        if (!suggestion.empty())
            msg += " (" + suggestion + ")";
        msg += " at " + std::string(location.file_name()) + ":" + std::to_string(location.line());
        return msg;
    }
    std::string expression_;
    std::string suggestion_;
    std::source_location location_;
};

using AssertHandlerFn = void (*)(
    const std::string &expression, const std::string &suggestion,
    const std::source_location &location);

inline AssertHandlerFn &get_assert_handler_ref() {
    static AssertHandlerFn g_assert_handler = nullptr;
    return g_assert_handler;
}
inline void set_assert_handler(AssertHandlerFn h) { get_assert_handler_ref() = h; }
inline AssertHandlerFn get_assert_handler() { return get_assert_handler_ref(); }

inline void handle_assert_failure(
    const std::string &expression, const std::string &suggestion,
    const std::source_location location = std::source_location::current()) {
    AssertHandlerFn handler = get_assert_handler();
    if (handler) {
        handler(expression, suggestion, location);
        std::abort();
    }
    std::cerr << "\n\033[1;31mCamel Debug Assertion Failed:\033[0m\n"
              << "\033[1;32m    Location   : \033[0m" << location.file_name() << "("
              << location.line() << ")\n"
              << "\033[1;32m    Function   : \033[0m" << location.function_name() << "\n"
              << "\033[1;32m    Expression : \033[0m" << expression << "\n"
              << "\033[1;32m    Suggestion : \033[0m" << suggestion << "\n";

#if HAS_STACKTRACE
    disable_windows_abort_dialogs();

    std::cerr << "\n\033[1;31mStack Trace:\033[0m\n";
    auto trace                  = std::stacktrace::current();
    constexpr size_t skip_front = 1;

    const size_t frame_count = trace.size();
    if (frame_count <= skip_front) {
        std::cerr << "(stack trace too short)\n";
    } else {
        for (size_t i = skip_front; i < frame_count; ++i) {
            std::cerr << "<\033[1;36m" << i << "\033[0m> " << trace[i] << "\n";
        }
    }
#else
    std::cerr << "\n\033[1;31mStack Trace:\033[0m (not available on this platform)\n";
#endif

    std::abort();
}

} // namespace cml

#ifdef NDEBUG

#define ASSERT(condition, message) ((void)0)

#else

#define ASSERT1(cond)                                                                              \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ::cml::handle_assert_failure(#cond, "");                                               \
        }                                                                                          \
    } while (false)

#define ASSERT2(cond, msg)                                                                         \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ::cml::handle_assert_failure(#cond, msg);                                              \
        }                                                                                          \
    } while (false)

#define GET_MACRO(_1, _2, NAME, ...) NAME
#define ASSERT(...) GET_MACRO(__VA_ARGS__, ASSERT2, ASSERT1)(__VA_ARGS__)

#endif
