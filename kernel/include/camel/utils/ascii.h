// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <format>
#include <string>

namespace ascii {

inline std::string underline(const std::string &from) {
    return std::format("\033[4m{}\033[0m", from);
}

inline std::string bold(const std::string &from) { return std::format("\033[1m{}\033[0m", from); }

inline std::string red(const std::string &from) { return std::format("\033[1;31m{}\033[0m", from); }

inline std::string green(const std::string &from) {
    return std::format("\033[1;32m{}\033[0m", from);
}

inline std::string yellow(const std::string &from) {
    return std::format("\033[1;33m{}\033[0m", from);
}

inline std::string blue(const std::string &from) {
    return std::format("\033[1;34m{}\033[0m", from);
}

} // namespace ascii
