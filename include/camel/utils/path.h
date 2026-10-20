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
 * Paths, at the one place where Camel's UTF-8 text meets the file system.
 *
 * A Camel `string` is UTF-8 (design principles 11.1), so every path the language hands to the
 * runtime — an `fs` argument, `CAMEL_HOME`, an argument of the command line — is UTF-8 text.
 * The system does not take that text directly: Windows identifies a file by UTF-16, and a
 * `std::filesystem::path` built from a narrow string is decoded in the process code page, which
 * is not UTF-8. Converting here, and only here, keeps the rest of the code in one encoding.
 */

#pragma once

#include "camel/utils/utf8.h"

#include <filesystem>
#include <string>
#include <string_view>

namespace camel::utils {

/// A path from the UTF-8 text a Camel string holds (a Windows path is built from UTF-16).
inline std::filesystem::path pathFromUtf8(std::string_view text) {
#ifdef _WIN32
    return std::filesystem::path(utf8::toWide(text));
#else
    return std::filesystem::path(std::string(text));
#endif
}

/// The UTF-8 text of a path: what a diagnostic prints and what a Camel string receives.
inline std::string pathToUtf8(const std::filesystem::path &path) {
#ifdef _WIN32
    return utf8::fromWide(path.native());
#else
    return path.native();
#endif
}

/// The UTF-8 text of a path with `/` separators, the form in which a document key or a URI
/// compares the same wherever the checkout is.
inline std::string genericPathToUtf8(const std::filesystem::path &path) {
    const std::u8string text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

/// The current working directory as UTF-8 text.
/// `std::filesystem::current_path` is the wide form on Windows (`_wgetcwd` underneath), so a
/// directory whose name the process code page cannot express is still read exactly. It throws
/// when the directory cannot be read, as every caller here expects.
inline std::string currentDirectory() {
    return pathToUtf8(std::filesystem::current_path());
}

} // namespace camel::utils