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
 * Updated: May. 24, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdlib>
#include <optional>
#include <string>

#ifdef _WIN32
#include <Windows.h>
#endif

inline std::string getEnv(const std::string &key, const std::string &defaultVal = "") {
#ifdef _WIN32
    char *buffer = nullptr;
    size_t size  = 0;
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

inline void setEnv(const std::string &key, const std::string &value) {
#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 1);
#endif
}

inline void unsetEnv(const std::string &key) {
#ifdef _WIN32
    _putenv_s(key.c_str(), "");
#else
    unsetenv(key.c_str());
#endif
}

class ScopedEnvVar {
  public:
    ScopedEnvVar(const std::string &key, std::optional<std::string> value)
        : key_(key), existed_(!getEnv(key).empty()), oldValue_(getEnv(key)) {
        if (value.has_value()) {
            setEnv(key_, value.value());
        } else {
            unsetEnv(key_);
        }
    }

    ~ScopedEnvVar() {
        if (existed_) {
            setEnv(key_, oldValue_);
        } else {
            unsetEnv(key_);
        }
    }

    ScopedEnvVar(const ScopedEnvVar &)            = delete;
    ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;

  private:
    std::string key_;
    bool existed_;
    std::string oldValue_;
};
