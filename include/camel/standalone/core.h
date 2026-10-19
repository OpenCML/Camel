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
 * Created: Oct. 14, 2026
 * Updated: Oct. 14, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * The standalone runtime of the programs std::cpp writes: faults, the recursion limit, the
 * end of a program, and the thread it runs on (docs/design/cpp-standalone/README.cn.md,
 * sections 7 and 3.3).
 *
 * Everything under include/camel/standalone/ is header-only and uses the C++20 standard
 * library and nothing else of Camel except three headers of camel/utils that are themselves
 * standard-only (number_text.h, int_div.h, utf8.h) and big_stack_thread.h. A generated program
 * compiles with `c++ -std=c++20 -I<camel>/include x.cpp` and does not link libcamel.
 *
 * Faults. A place of the program where a fault can happen is a Site, a constant the generated
 * program defines. It holds the report of that fault in the two formats of `camel`
 * (CAMEL_ERROR_FORMAT=text, the default, and =short), rendered by the compiler when it wrote
 * the program, so that the text is the one the interpreters print, byte for byte. What is
 * known only when the fault happens (an index, a length, the limit) is written there as a
 * placeholder, the byte 0x1F followed by one digit, the position of the value among the
 * arguments of fault().
 */

#pragma once

#include "camel/utils/big_stack_thread.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace camel::standalone {

// ---- Faults ----

/// A place where a fault can happen, and what a fault there reports (see the file comment).
/// An `attempt` turns the fault into an error value of kind, message and location.
struct Site {
    const char *text;     // the report in the default format
    const char *brief;    // the report in the one-line format
    const char *kind;     // the diagnostic's name without its prefix: "DivisionByZero"
    const char *message;  // its message
    const char *location; // `file.cml:line:col`, the file without its directory
};

/// A place where an operator of a runtime module runs (bridge.h): what kind of fault it raises
/// is known only when it does, so the report is put together then from the diagnostic the
/// runtime gives and these parts, rendered by the compiler like a Site.
struct Place {
    const char *excerpt;  // the source excerpt of the default format: `  --> file:line:col` ...
    const char *where;    // the start of the one-line format: `(module)file:line:col`
    const char *location; // `file.cml:line:col`, the file without its directory
};

/// The byte that, followed by a digit, stands for a value only known when the fault happens.
inline constexpr char kFaultValueMark = '\x1f';

/// `text` with the values in place of their marks.
inline std::string withValues(std::string_view text, const std::vector<std::string> &values) {
    std::string out;
    out.reserve(text.size() + 32);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == kFaultValueMark && i + 1 < text.size()) {
            const std::size_t k = static_cast<std::size_t>(text[i + 1] - '0');
            if (k < values.size()) {
                out += values[k];
            }
            ++i;
            continue;
        }
        out.push_back(text[i]);
    }
    return out;
}

/// A fault: it ends the program with a located report unless an `attempt` catches it, which
/// makes it an error value of its kind, message and location.
class Fault {
  public:
    struct Texts {
        std::string text;     // the report in the default format
        std::string brief;    // the report in the one-line format
        std::string kind;     // for an error value
        std::string message;  // for an error value
        std::string location; // for an error value
    };

    explicit Fault(Texts texts) : texts_(std::move(texts)) {}

    /// The report in the default format, or in the one-line format.
    const std::string &report(bool brief) const { return brief ? texts_.brief : texts_.text; }
    const std::string &kind() const { return texts_.kind; }
    const std::string &message() const { return texts_.message; }
    const std::string &location() const { return texts_.location; }

  private:
    Texts texts_;
};

/// Raises a fault at `site`; `values` are the texts the report's marks stand for.
[[noreturn]] inline void fault(const Site &site, const std::vector<std::string> &values = {}) {
    throw Fault(Fault::Texts{
        withValues(site.text, values),
        withValues(site.brief, values),
        site.kind,
        withValues(site.message, values),
        site.location});
}

/// `exit(n)`: the program ends with exit code `n` once the output is written. A C++ exception,
/// so that the stack unwinds (and no `attempt` takes it for a fault).
struct ExitRequest {
    int code;
};

[[noreturn]] inline void exit(int64_t code) { throw ExitRequest{static_cast<int>(code)}; }

// ---- The recursion limit ----

/// The number of calls that are open, not in tail position, below the current function (design
/// principles 2.9; camel/execute/limits.h). It is an argument of every generated function that
/// makes calls, so that it lives in a register.
using Depth = uint32_t;

/// The limit in effect: the one the program was translated with, unless the environment
/// variable CAMEL_MAX_RECURSION_DEPTH gives another (set by run()).
inline uint32_t recursionLimit = 1000;

/// The largest limit: what camel/execute/limits.h accepts where the native stack is guarded.
inline constexpr uint32_t kMaxRecursionLimit = 1000000;

/// One more open call, made at `site` from a function `depth` calls deep. With a limit L, the
/// L-th open call is the last that is made (the entry's call of `main` is not counted: main
/// runs at depth 0). The report names the calling graph and the limit (value 0).
inline Depth call(Depth depth, const Site &site) {
    if (depth >= recursionLimit) [[unlikely]] {
        fault(site, {std::to_string(recursionLimit)});
    }
    return depth + 1;
}

// ---- Output ----

/// Writes `text` to standard output as it is (no line-end translation, see run()).
inline void write(std::string_view text) { std::fwrite(text.data(), 1, text.size(), stdout); }

// ---- Running a program ----

namespace detail {

/// A limit written in the environment: a decimal integer from 1 to kMaxRecursionLimit;
/// anything else is no value (as camel/execute/limits.cpp reads it).
inline bool limitFromEnvironment(uint32_t &out) {
    const char *raw = std::getenv("CAMEL_MAX_RECURSION_DEPTH");
    if (raw == nullptr || *raw == '\0') {
        return false;
    }
    uint64_t value = 0;
    for (const char *p = raw; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = value * 10 + static_cast<uint64_t>(*p - '0');
        if (value > kMaxRecursionLimit) {
            return false;
        }
    }
    if (value == 0) {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

/// Whether reports use the one-line format (CAMEL_ERROR_FORMAT=short, as for `camel`).
inline bool briefReports() {
    const char *format = std::getenv("CAMEL_ERROR_FORMAT");
    return format != nullptr && std::string_view(format) == "short";
}

/// The native stack the program runs on: room for `limit` open calls of a generous frame
/// size, at least 64 MiB and at most 1 GiB. Only the pages a deep recursion touches are used.
inline std::size_t stackBytesFor(uint32_t limit) {
    constexpr std::size_t kBytesPerCall = 512;
    constexpr std::size_t kMin          = std::size_t{64} << 20;
    constexpr std::size_t kMax          = std::size_t{1} << 30;
    return std::clamp(static_cast<std::size_t>(limit) * kBytesPerCall, kMin, kMax);
}

} // namespace detail

/// Runs the entry of a generated program and gives the process's exit code.
///
/// `entry` returns the exit code the program's `main` gives (the generated code decides it
/// from main's declared type, design principles 2.10). A fault ends the program: the output
/// written so far is flushed, then the report is written to standard output (where `camel`
/// writes its diagnostics) and the exit code is 1. `exit(n)` ends it with n.
///
/// The program runs on a thread of its own whose stack has room for the recursion limit;
/// `translatedLimit` is the limit the program was translated with.
template <typename Entry> int run(uint32_t translatedLimit, Entry &&entry) {
#ifdef _WIN32
    // The program writes the same bytes on every platform (design principles 2.11): no CRLF.
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    recursionLimit = translatedLimit;
    if (uint32_t fromEnvironment = 0; detail::limitFromEnvironment(fromEnvironment)) {
        recursionLimit = fromEnvironment;
    }

    int code = 0;
    camel::utils::BigStackThread thread(detail::stackBytesFor(recursionLimit), [&] {
        try {
            code = entry();
        } catch (const Fault &f) {
            std::fflush(stdout);
            write(f.report(detail::briefReports()));
            write("\n");
            code = 1;
        } catch (const ExitRequest &request) {
            code = request.code;
        }
    });
    thread.join();
    std::fflush(stdout);
    return code;
}

} // namespace camel::standalone
