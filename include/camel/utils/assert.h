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

/// The release path of `GUARD`: the same report and the same abort as an `ASSERT`, in every build.
///
/// `expression` is the condition as written; `reading` is what a person needs in order to know what
/// went wrong -- the value and the bound it crossed, not just the name of the rule.
[[noreturn]] void fail_invariant(
    const std::string &expression, const std::string &reading, const std::source_location location =
                                                                  std::source_location::current());

} // namespace cml

#if defined(__GNUC__) || defined(__clang__)
#define CAMEL_UNLIKELY(condition) __builtin_expect(!!(condition), 0)
#else
#define CAMEL_UNLIKELY(condition) (condition)
#endif

/// A check that is alive in every build, for a condition the code *relies* on being true.
///
/// `ASSERT` records what the code believes; under `NDEBUG` it is `((void)0)`, so a site whose
/// failure makes the code write past a buffer, follow a stale pointer or read a value as the wrong
/// type has no check at all in the build that ships. `GUARD` is that check: unchanged condition at
/// the same place, a report that names the value and the bound, and an abort. The branch it adds is
/// never taken, so it predicts and costs what a predicted branch costs.
///
/// Use it where `ASSERT` would be lying about being a guard; write `reading` so that the line says
/// which value crossed which bound.
///
/// Its report is headed `Camel Invariant Violation` rather than `Debug Assertion`: this check is
/// alive in the build that ships, so its heading must not read like a build-time reminder.
#define GUARD(condition, reading)                                                                  \
    do {                                                                                           \
        if (CAMEL_UNLIKELY(!(condition))) {                                                        \
            ::cml::fail_invariant(#condition, (reading));                                          \
        }                                                                                          \
    } while (false)

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