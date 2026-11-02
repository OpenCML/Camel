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

#if defined(CAMEL_STACK_TRACE) && !defined(_WIN32)
#include <cxxabi.h>
#include <execinfo.h>
#endif

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

namespace {
#if defined(CAMEL_STACK_TRACE) && !defined(_WIN32)
/// The frames of the current call. `backtrace` answers addresses; `backtrace_symbols` names them when the
/// library exports the symbol (libcamel.so does), and the mangled part is demangled here so a reader sees
/// `camel::core::type::TupleType::typeAt`. **No line numbers** -- those need `addr2line`, which this omits.
void printBacktrace() {
    void *frames[64];
    const int count = ::backtrace(frames, 64);
    if (count <= 0) {
        std::cerr << "(no frames)\n";
        return;
    }
    char **names = ::backtrace_symbols(frames, count);
    for (int i = 1; i < count; ++i) {
        std::string line = names != nullptr ? names[i] : std::string("?");
        const auto open  = line.find('(');
        const auto plus  = line.find('+', open == std::string::npos ? 0 : open);
        if (open != std::string::npos && plus != std::string::npos && plus > open + 1) {
            const std::string mangled = line.substr(open + 1, plus - open - 1);
            int status                = 0;
            if (char *pretty = abi::__cxa_demangle(mangled.c_str(), nullptr, nullptr, &status);
                status == 0 && pretty != nullptr) {
                line = line.substr(0, open + 1) + pretty + line.substr(plus);
                std::free(pretty);
            }
        }
        std::cerr << "<\033[1;36m" << i << "\033[0m> " << line << "\n";
    }
    if (names != nullptr) {
        std::free(names);
    }
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
    // "not available on this platform" was wrong: the tracer is our `#else`, and it can be built. It is
    // opt-in because a trace needs the frames, not because the platform cannot give them -- and it does
    // not need `-rdynamic` (see CMakeLists.txt for why that option is forbidden here).
#if defined(CAMEL_STACK_TRACE)
    printBacktrace();
#else
    std::cerr << "\n\033[1;31mStack Trace:\033[0m (stack trace disabled at build time -- rebuild with "
                 "`-D CAMEL_STACK_TRACE=ON`, or the gate's `-StackTrace`)\n";
#endif
#endif

    std::abort();
}

void fail_invariant(
    const std::string &expression, const std::string &reading,
    const std::source_location location) {
    AssertHandlerFn handler = get_assert_handler();
    if (handler) {
        handler(expression, reading, location);
        std::abort();
    }
    // Not "Debug", and not "Assertion": this one is here in the build that ships, so its report has
    // to read like a diagnosis of the program rather than like a developer's reminder.
    std::cerr << "\n\033[1;31mCamel Invariant Violation:\033[0m\n"
              << "\033[1;32m    Location   : \033[0m" << location.file_name() << "("
              << location.line() << ")\n"
              << "\033[1;32m    Function   : \033[0m" << location.function_name() << "\n"
              << "\033[1;32m    Guarantee  : \033[0m" << expression << "\n"
              << "\033[1;32m    Reading    : \033[0m" << reading << "\n";

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
    // "not available on this platform" was wrong: the tracer is our `#else`, and it can be built. It is
    // opt-in because a trace needs the frames, not because the platform cannot give them -- and it does
    // not need `-rdynamic` (see CMakeLists.txt for why that option is forbidden here).
#if defined(CAMEL_STACK_TRACE)
    printBacktrace();
#else
    std::cerr << "\n\033[1;31mStack Trace:\033[0m (stack trace disabled at build time -- rebuild with "
                 "`-D CAMEL_STACK_TRACE=ON`, or the gate's `-StackTrace`)\n";
#endif
#endif

    std::abort();
}

} // namespace cml