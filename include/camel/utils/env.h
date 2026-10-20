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

#include "camel/utils/utf8.h"

#include <cstdlib>
#include <optional>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

/*
 * The process environment, at the one place where it is read and written.
 *
 * Names and values are UTF-8, like every Camel string (design principles 11.1). On Windows the
 * environment is UTF-16: reading it through the narrow C runtime decodes it in the process code
 * page, so a value or a `PATH` entry that page cannot express (a user name in Chinese, an emoji
 * in a directory name) came back as bytes that are not UTF-8 at all. The wide interface returns
 * the text exactly; `getEnv` converts it once, here.
 */

inline std::string getEnv(const std::string &key, const std::string &defaultVal = "") {
#ifdef _WIN32
    const std::wstring name = camel::utf8::toWide(key);
    std::wstring value(64, L'\0');
    for (;;) {
        const DWORD written =
            GetEnvironmentVariableW(name.c_str(), value.data(), static_cast<DWORD>(value.size()));
        if (written == 0) {
            // Not set, or set to the empty string: Windows has no variable with an empty value.
            return defaultVal;
        }
        if (written < value.size()) {
            value.resize(written);
            return camel::utf8::fromWide(value);
        }
        // The value grew between the two calls; `written` is the size needed with the terminator.
        value.resize(written);
    }
#else
    const char *val = std::getenv(key.c_str());
    return val ? std::string(val) : defaultVal;
#endif
}

inline void setEnv(const std::string &key, const std::string &value) {
#ifdef _WIN32
    const std::wstring name = camel::utf8::toWide(key);
    const std::wstring text = camel::utf8::toWide(value);
    SetEnvironmentVariableW(name.c_str(), text.c_str());
    // The process environment block is what a child process inherits (set just above); the C
    // runtime keeps its own copy, which is what Python reads (`_wgetenv` for PYTHONHOME and
    // PYTHONPATH). Both are updated so that the two agree.
    _wputenv_s(name.c_str(), text.c_str());
#else
    setenv(key.c_str(), value.c_str(), 1);
#endif
}

inline void unsetEnv(const std::string &key) {
#ifdef _WIN32
    const std::wstring name = camel::utf8::toWide(key);
    SetEnvironmentVariableW(name.c_str(), nullptr);
    _wputenv_s(name.c_str(), L"");
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
