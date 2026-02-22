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
 * Updated: Feb. 22, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/utils/log.h"

#include <mutex>

static Logger defaultLogger("");

Logger &GetDefaultLogger() { return defaultLogger; }

#ifndef NDEBUG
Logger::Level Logger::globalLogLevel_ = Logger::Level::Debug;
bool Logger::verboseEnabled_          = false;
std::ofstream Logger::logFile_;
std::mutex Logger::logMutex_;

void Logger::SetVerbose(bool enable) { verboseEnabled_ = enable; }

void Logger::SetLogFile(const std::string &filename) {
    std::lock_guard<std::mutex> lock(logMutex_);
    if (logFile_.is_open()) {
        logFile_.close();
    }
    logFile_.open(filename, std::ios::app);
}

Logger::Level GetGlobalLogLevel() { return Logger::globalLogLevel_; }
void SetGlobalLogLevel(Logger::Level level) { Logger::globalLogLevel_ = level; }
bool IsVerboseEnabled() { return Logger::verboseEnabled_; }

static std::string levelToTag(Logger::Level level) {
    switch (level) {
    case Logger::Level::Info:
        return "\033[1;32mINFO \033[0m";
    case Logger::Level::Warn:
        return "\033[1;33mWARN \033[0m";
    case Logger::Level::Debug:
        return "\033[1;36mDEBUG\033[0m";
    case Logger::Level::Error:
        return "\033[1;31mERROR\033[0m";
    default:
        return "UNKNOWN";
    }
}

static std::string levelToPlain(Logger::Level level) {
    switch (level) {
    case Logger::Level::Info:
        return "INFO";
    case Logger::Level::Warn:
        return "WARN";
    case Logger::Level::Debug:
        return "DEBUG";
    case Logger::Level::Error:
        return "ERROR";
    default:
        return "UNKNOWN";
    }
}

void Logger_DoLog(
    const std::string &scope, Logger::Level effectiveLevel, Logger::Level level,
    const std::string &message) {
    if (level < effectiveLevel)
        return;
    std::string tag         = levelToTag(level);
    std::string plainTag    = levelToPlain(level);
    std::string fullMessage = std::format("[{}] <{}> {}", plainTag, scope, message);

    std::lock_guard<std::mutex> lock(Logger::logMutex_);
    std::cout << std::format("[{}] <{}> {}\n", tag, scope, message) << std::flush;
    if (Logger::logFile_.is_open()) {
        Logger::logFile_ << fullMessage << std::endl;
    }
}

static const std::string filteredLoggerScope;
const std::string &GetFilteredLoggerScope() { return filteredLoggerScope; }
#endif
