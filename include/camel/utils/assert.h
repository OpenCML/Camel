// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The assertion an internal invariant is written against.
 *
 * The failure path lives in `src/utils/assert.cpp`: printing it needs `<iostream>` (and, with the
 * standard library this project builds against, `<format>` behind it), and this header is included
 * by every translation unit -- so the printing stays in the one place that can fail instead of
 * being parsed by each of them. `ASSERT` compiles to nothing under `NDEBUG`.
 */

#pragma once

#include <source_location>
#include <stdexcept>
#include <string>

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

/// Reports the failed assertion to the handler, or to `stderr` with the stack trace when no
/// handler is installed, then aborts. Defined in `src/utils/assert.cpp`.
[[noreturn]] void handle_assert_failure(
    const std::string &expression, const std::string &suggestion,
    const std::source_location location = std::source_location::current());

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