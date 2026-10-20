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
 * Created: Oct. 20, 2026
 * Updated: Oct. 20, 2026
 * Supported by: National Key Research and Development Program of China
 */

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