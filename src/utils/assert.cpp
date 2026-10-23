// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The assertion failure path: the handler dispatch, the `stderr` report and the stack trace.
 *
 * This is the only translation unit that needs the iostreams for an assertion, which is what keeps
 * `<iostream>` (and the format machinery behind it) out of `camel/utils/assert.h` -- a header every
 * translation unit includes.
 */

#include "camel/utils/assert.h"

#include <cstdlib>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <crtdbg.h>
#include <stacktrace>
#endif

namespace cml {
namespace {

#ifdef _WIN32
/// A failed assertion is a diagnosis, not a crash to be reported to the operating system.
void disableWindowsAbortDialogs() {
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT, 0);
    _CrtSetReportMode(_CRT_ERROR, 0);
    _CrtSetReportMode(_CRT_WARN, 0);
}
#endif

} // namespace

void handle_assert_failure(
    const std::string &expression, const std::string &suggestion,
    const std::source_location location) {
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

#ifdef _WIN32
    disableWindowsAbortDialogs();

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