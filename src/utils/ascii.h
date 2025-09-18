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
 * See the the MIT license for more details
 *
 * Author: Zhenjie Wei
 * Created: Sep. 17, 2025
 * Updated: Sep. 17, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <format>
#include <string>

namespace ascii {

inline std::string underline(const std::string &from) {
    return std::format("\033[4m{}\033[0m", from);
}

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