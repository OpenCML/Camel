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
 * Created: Jul. 03, 2025
 * Updated: Jul. 03, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <string>
#include <vector>
#include <sstream>

template <typename T>
inline std::string join_list(const std::vector<T> &list, const std::string &separator) {
    std::string result;
    for (size_t i = 0; i < list.size(); ++i) {
        result += list[i];
        if (i != list.size() - 1) {
            result += separator;
        }
    }
    return result;
}

inline std::vector<std::string> split(const std::string &str, char delim) {
    std::vector<std::string> result;
    std::stringstream ss(str);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty())
            result.push_back(item);
    }
    return result;
}

inline std::string join(const std::vector<std::string> &parts, char delim) {
    std::ostringstream oss;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0)
            oss << delim;
        oss << parts[i];
    }
    return oss.str();
}