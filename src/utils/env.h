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
 * Created: Sep. 01, 2025
 * Updated: Sep. 01, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

std::string getEnv(const std::string &key, const std::string &defaultVal = "") {
#ifdef _WIN32
    char *buffer = nullptr;
    size_t size = 0;
    if (_dupenv_s(&buffer, &size, key.c_str()) == 0 && buffer != nullptr) {
        std::string value(buffer);
        free(buffer);
        return value;
    }
    return defaultVal;
#else
    const char *val = std::getenv(key.c_str());
    return val ? std::string(val) : defaultVal;
#endif
}
