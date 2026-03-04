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
 * Created: Sep. 04, 2025
 * Updated: Mar. 04, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <format>
#include <memory>
#include <optional>
#include <string>

#if defined(_WIN32) && !defined(CAMEL_DLL_EXPORTS)
#define CAMEL_LOG_API __declspec(dllimport)
#elif defined(_WIN32)
#define CAMEL_LOG_API __declspec(dllexport)
#else
#define CAMEL_LOG_API
#endif

#ifdef NDEBUG

class Logger {
  public:
    enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };

    static void SetLogLevel(Level) {}
    /// Add an output stream; returns a handle for removal. Caller keeps stream valid.
    static size_t AddOutputStream(std::ostream *) { return 0; }
    /// Remove a stream by handle returned from AddOutputStream.
    static void RemoveOutputStream(size_t) {}
    /// Write a raw line to all registered streams (e.g. for debugger messages).
    static void WriteToAllStreams(const std::string &) {}
    static void SetVerbose(bool) {}
    static void SetColorEnabled(bool) {}

    Logger(
        const std::string & = "", std::shared_ptr<Logger> parent = nullptr,
        std::optional<Level> localLevel = std::nullopt) {}

    Logger in(const std::string &, std::optional<Level> = std::nullopt) const { return Logger{}; }

    template <typename... Args> void info(std::format_string<Args...>, Args &&...) const {}

    template <typename... Args> void warn(std::format_string<Args...>, Args &&...) const {}

    template <typename... Args> void debug(std::format_string<Args...>, Args &&...) const {}

    template <typename... Args> void error(std::format_string<Args...>, Args &&...) const {}
};

/** Returns the default logger instance. Exported as function for delay-load compatibility. */
CAMEL_LOG_API Logger &GetDefaultLogger();

#else

#include <algorithm>
#include <chrono>
#include <ctime>
#include <iostream>
#include <mutex>
#include <vector>

// Forward declarations and function API (must appear before Logger for delay-load compatibility)
class Logger;
enum class LogLevel : int { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };
CAMEL_LOG_API const std::string &GetFilteredLoggerScope();
CAMEL_LOG_API LogLevel GetGlobalLogLevel();
CAMEL_LOG_API void SetGlobalLogLevel(LogLevel level);
CAMEL_LOG_API bool IsVerboseEnabled();
CAMEL_LOG_API bool IsColorEnabled();
CAMEL_LOG_API void SetColorEnabled(bool enable);
CAMEL_LOG_API void Logger_DoLog(
    const std::string &scope, LogLevel effectiveLevel, LogLevel level, const std::string &message);
CAMEL_LOG_API Logger &GetDefaultLogger();

class Logger {
  public:
    using Level = LogLevel;

    friend CAMEL_LOG_API const std::string &GetFilteredLoggerScope();
    friend CAMEL_LOG_API LogLevel GetGlobalLogLevel();
    friend CAMEL_LOG_API void SetGlobalLogLevel(LogLevel level);
    friend CAMEL_LOG_API bool IsVerboseEnabled();
    friend CAMEL_LOG_API bool IsColorEnabled();
    friend CAMEL_LOG_API void SetColorEnabled(bool);
    friend CAMEL_LOG_API void Logger_DoLog(
        const std::string &scope, LogLevel effectiveLevel, LogLevel level,
        const std::string &message);
    friend CAMEL_LOG_API Logger &GetDefaultLogger();

    static void SetLogLevel(Level level) { SetGlobalLogLevel(level); }

    /// Add an output stream; returns a handle for removal. Caller keeps stream valid.
    static CAMEL_LOG_API size_t AddOutputStream(std::ostream *os);
    /// Remove a stream by handle returned from AddOutputStream.
    static CAMEL_LOG_API void RemoveOutputStream(size_t handle);
    /// Write a raw line to all registered streams (no level/scope).
    static CAMEL_LOG_API void WriteToAllStreams(const std::string &message);

    static CAMEL_LOG_API void SetVerbose(bool enable);
    static void SetColorEnabled(bool enable) { ::SetColorEnabled(enable); }

    Logger(
        const std::string &scope = "", std::shared_ptr<Logger> parent = nullptr,
        std::optional<Level> localLevel = std::nullopt)
        : scope_(scope), parent_(std::move(parent)), localLogLevel_(localLevel) {
        if (localLogLevel_) {
            if (parent_) {
                effectiveLogLevel_ = std::min(*localLogLevel_, parent_->effectiveLogLevel_);
            } else {
                effectiveLogLevel_ = std::min(*localLogLevel_, GetGlobalLogLevel());
            }
        } else {
            if (parent_) {
                effectiveLogLevel_ = parent_->effectiveLogLevel_;
            } else {
                effectiveLogLevel_ = GetGlobalLogLevel();
            }
        }
    }

    Logger in(const std::string &subScope, std::optional<Level> localLevel = std::nullopt) const {
        return Logger(
            scope_.empty() ? subScope : (scope_ + "." + subScope),
            std::make_shared<Logger>(*this),
            localLevel);
    }

    bool filtered() const {
#ifndef NDEBUG
        const std::string &scope = GetFilteredLoggerScope();
        return scope.empty() || this->scope_ == scope;
#else
        return true;
#endif
    }

    template <typename... Args> void info(std::format_string<Args...> fmt, Args &&...args) const {
        if (IsVerboseEnabled() && filtered()) {
            Logger_DoLog(
                scope_,
                effectiveLogLevel_,
                Level::Info,
                std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <typename... Args> void warn(std::format_string<Args...> fmt, Args &&...args) const {
        if (IsVerboseEnabled() && filtered()) {
            Logger_DoLog(
                scope_,
                effectiveLogLevel_,
                Level::Warn,
                std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <typename... Args> void debug(std::format_string<Args...> fmt, Args &&...args) const {
        if (IsVerboseEnabled() && filtered()) {
            Logger_DoLog(
                scope_,
                effectiveLogLevel_,
                Level::Debug,
                std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <typename... Args> void error(std::format_string<Args...> fmt, Args &&...args) const {
        if (IsVerboseEnabled() && filtered()) {
            Logger_DoLog(
                scope_,
                effectiveLogLevel_,
                Level::Error,
                std::format(fmt, std::forward<Args>(args)...));
        }
    }

  private:
    std::string scope_;
    std::shared_ptr<Logger> parent_;
    std::optional<Level> localLogLevel_;
    Level effectiveLogLevel_;

    static Level globalLogLevel_;
    static bool verboseEnabled_;
    static bool colorEnabled_;
    static std::mutex logMutex_;
    using StreamEntry = std::pair<size_t, std::ostream *>;
    static std::vector<StreamEntry> outputStreams_;
    static size_t nextStreamHandle_;

    static std::string levelToTag(Level level) {
        switch (level) {
        case Level::Info:
            return "\033[1;32mINFO \033[0m";
        case Level::Warn:
            return "\033[1;33mWARN \033[0m";
        case Level::Debug:
            return "\033[1;36mDEBUG\033[0m";
        case Level::Error:
            return "\033[1;31mERROR\033[0m";
        default:
            return "UNKNOWN";
        }
    }

    static std::string levelToPlain(Level level) {
        switch (level) {
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Debug:
            return "DEBUG";
        case Level::Error:
            return "ERROR";
        default:
            return "UNKNOWN";
        }
    }
};

#endif // NDEBUG
