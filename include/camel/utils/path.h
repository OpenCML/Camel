// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

/// The module name a module file has: its name without the extension (design principles 10.2: a
/// file is a module, and the module is named by the file, with no declaration needed). `counts.cml`
/// is the module `counts` and `shapes.cmo` is `shapes`; only the last extension is dropped
/// (`a.b.cml` is the module `a.b`), and a name with no extension is used as it stands. Nothing is
/// looked up or read here: the name follows from the path alone, which is why every tool that
/// loads an entry file names the entry module the same way.
inline std::string moduleNameOfFile(std::string_view path) {
    return pathToUtf8(pathFromUtf8(path).stem());
}

} // namespace camel::utils