// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The process command line as UTF-8, at the one place where a tool reads it.
 *
 * Windows hands the command line to a program as UTF-16. `main(int, char **)` receives a copy
 * decoded in the process code page, which drops or misreads a character that page does not
 * contain — an argument naming a file under a Chinese user name, for instance. A tool therefore
 * has a `wmain` on Windows and converts its arguments here, and an ordinary `main` elsewhere;
 * both forms meet the same `utf8Arguments` and the rest of the tool sees UTF-8 strings only.
 */

#pragma once

#include "camel/utils/utf8.h"

#include <string>
#include <vector>

namespace camel::utils {

/// The arguments of a `wmain`, as UTF-8 (Windows).
inline std::vector<std::string> utf8Arguments(int argc, wchar_t *const argv[]) {
    std::vector<std::string> arguments;
    arguments.reserve(argc > 0 ? static_cast<size_t>(argc) : 0);
    for (int i = 0; i < argc; ++i) {
        arguments.push_back(utf8::fromWide(argv[i]));
    }
    return arguments;
}

/// The arguments of a `main`, as UTF-8 (every platform; on Windows the narrow form is what the
/// system decoded in the process code page, so a tool reaches this one only off Windows).
inline std::vector<std::string> utf8Arguments(int argc, char *const argv[]) {
    std::vector<std::string> arguments;
    arguments.reserve(argc > 0 ? static_cast<size_t>(argc) : 0);
    for (int i = 0; i < argc; ++i) {
        arguments.push_back(argv[i]);
    }
    return arguments;
}

} // namespace camel::utils