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
 * Updated: Sep. 04, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <format>
#include <string>
#include <optional>

#ifdef NDEBUG

class Logger {
  public:
    enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };

    static void SetLogLevel(Level) {}
    static void SetLogFile(const std::string &) {}
    static void SetVerbose(bool) {}

    Logger(
        const std::string & = "", std::shared_ptr<Logger> parent = nullptr,
        std::optional<Level> localLevel = std::nullopt) {}

    Logger in(const std::string &, std::optional<Level> = std::nullopt) const { return Logger{}; }

    template <typename... Args> void info(std::format_string<Args...>, Args &&...) const {}

    template <typename... Args> void warn(std::format_string<Args...>, Args &&...) const {}

    template <typename... Args> void debug(std::format_string<Args...>, Args &&...) const {}

    template <typename... Args> void error(std::format_string<Args...>, Args &&...) const {}
};

#else

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>

class Logger {
  public:
    enum class Level { Debug = 0, Info = 1, Warn = 2, Error = 3, Off = 4 };

    static void SetLogLevel(Level level) { globalLogLevel_ = level; }

    static void SetLogFile(const std::string &filename) {
        std::lock_guard<std::mutex> lock(logMutex_);
        if (logFile_.is_open()) {
            logFile_.close();
        }
        logFile_.open(filename, std::ios::app);
    }

    static void SetVerbose(bool enable) { verboseEnabled_ = enable; }

    Logger(
        const std::string &scope = "", std::shared_ptr<Logger> parent = nullptr,
        std::optional<Level> localLevel = std::nullopt)
        : scope_(scope), parent_(std::move(parent)), localLogLevel_(localLevel) {
        if (localLogLevel_) {
            if (parent_) {
                effectiveLogLevel_ = std::min(*localLogLevel_, parent_->effectiveLogLevel_);
            } else {
                effectiveLogLevel_ = std::min(*localLogLevel_, globalLogLevel_);
            }
        } else {
            if (parent_) {
                effectiveLogLevel_ = parent_->effectiveLogLevel_;
            } else {
                effectiveLogLevel_ = globalLogLevel_;
            }
        }
    }

    Logger in(const std::string &subScope, std::optional<Level> localLevel = std::nullopt) const {
        return Logger(
            scope_.empty() ? subScope : (scope_ + "." + subScope),
            std::make_shared<Logger>(*this),
            localLevel);
    }

    template <typename... Args> void info(std::format_string<Args...> fmt, Args &&...args) const {
        if (verboseEnabled_) {
            log(Level::Info, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <typename... Args> void warn(std::format_string<Args...> fmt, Args &&...args) const {
        if (verboseEnabled_) {
            log(Level::Warn, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <typename... Args> void debug(std::format_string<Args...> fmt, Args &&...args) const {
        if (verboseEnabled_) {
            log(Level::Debug, std::format(fmt, std::forward<Args>(args)...));
        }
    }

    template <typename... Args> void error(std::format_string<Args...> fmt, Args &&...args) const {
        if (verboseEnabled_) {
            log(Level::Error, std::format(fmt, std::forward<Args>(args)...));
        }
    }

  private:
    std::string scope_;
    std::shared_ptr<Logger> parent_;
    std::optional<Level> localLogLevel_;
    Level effectiveLogLevel_;

    static inline Level globalLogLevel_ = Level::Debug;
    static inline bool verboseEnabled_ = false;
    static inline std::ofstream logFile_;
    static inline std::mutex logMutex_;

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

    void log(Level level, const std::string &message) const {
        if (level < effectiveLogLevel_)
            return;

        std::string tag = levelToTag(level);
        std::string plainTag = levelToPlain(level);
        std::string fullMessage = std::format("[{}] <{}> {}", plainTag, scope_, message);

        std::lock_guard<std::mutex> lock(logMutex_);

        std::cout << std::format("[{}] <{}> {}\n", tag, scope_, message);

        if (logFile_.is_open()) {
            logFile_ << fullMessage << std::endl;
        }
    }
};

#endif // NDEBUG

extern Logger l;