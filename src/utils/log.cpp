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

#include "camel/utils/log.h"

#include <algorithm>
#include <mutex>

static Logger defaultLogger("");

Logger &GetDefaultLogger() { return defaultLogger; }

#ifndef NDEBUG
Logger::Level Logger::globalLogLevel_ = Logger::Level::Debug;
bool Logger::verboseEnabled_          = false;
bool Logger::colorEnabled_            = true;
std::mutex Logger::logMutex_;
std::vector<Logger::StreamEntry> Logger::outputStreams_;
size_t Logger::nextStreamHandle_ = 0;

void Logger::SetVerbose(bool enable) { verboseEnabled_ = enable; }

size_t Logger::AddOutputStream(std::ostream *os) {
    if (!os)
        return 0;
    std::lock_guard<std::mutex> lock(logMutex_);
    size_t h = ++nextStreamHandle_;
    outputStreams_.emplace_back(h, os);
    return h;
}

void Logger::RemoveOutputStream(size_t handle) {
    if (handle == 0)
        return;
    std::lock_guard<std::mutex> lock(logMutex_);
    outputStreams_.erase(
        std::remove_if(
            outputStreams_.begin(),
            outputStreams_.end(),
            [handle](const StreamEntry &e) { return e.first == handle; }),
        outputStreams_.end());
}

void Logger::WriteToAllStreams(const std::string &message) {
    std::lock_guard<std::mutex> lock(logMutex_);
    for (const auto &e : outputStreams_) {
        if (e.second) {
            *e.second << message << '\n' << std::flush;
        }
    }
}

Logger::Level GetGlobalLogLevel() { return Logger::globalLogLevel_; }
void SetGlobalLogLevel(Logger::Level level) { Logger::globalLogLevel_ = level; }
bool IsVerboseEnabled() { return Logger::verboseEnabled_; }
bool IsColorEnabled() { return Logger::colorEnabled_; }
void SetColorEnabled(bool enable) { Logger::colorEnabled_ = enable; }

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
    std::string tag         = Logger::colorEnabled_ ? levelToTag(level) : levelToPlain(level);
    std::string fullMessage = std::format("[{}] <{}> {}", tag, scope, message);

    std::lock_guard<std::mutex> lock(Logger::logMutex_);
    for (const auto &e : Logger::outputStreams_) {
        if (e.second) {
            *e.second << fullMessage << '\n' << std::flush;
        }
    }
}

static const std::string filteredLoggerScope;
const std::string &GetFilteredLoggerScope() { return filteredLoggerScope; }
#endif
