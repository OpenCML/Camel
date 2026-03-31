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
 * Updated: Apr. 01, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <cstdint>
#include <format>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(_WIN32) && !defined(CAMEL_DLL_EXPORTS)
#define CAMEL_LOG_API __declspec(dllimport)
#elif defined(_WIN32)
#define CAMEL_LOG_API __declspec(dllexport)
#else
#define CAMEL_LOG_API
#endif

/// Numeric order defines severity for threshold checks: emit when
/// \c static_cast<int>(messageLevel) >= static_cast<int>(globalThreshold)
/// (with \c Off handled separately).
enum class LogLevel : int {
    Trace = 0,
    Debug = 1,
    Info  = 2,
    Warn  = 3,
    Fatal = 4,
    Off   = 5,
};

enum class LogCategoryPreset : std::uint8_t { None = 0, Wall = 1, Extra = 2 };

class Logger {
  public:
    using Level = LogLevel;

    static CAMEL_LOG_API void SetGlobalLogThreshold(LogLevel t);
    static CAMEL_LOG_API LogLevel GetGlobalLogThreshold();

    static void SetLogLevel(Level t) { SetGlobalLogThreshold(t); }
    static Level GetLogLevel() { return GetGlobalLogThreshold(); }

    static CAMEL_LOG_API void SetLogCategoryPreset(LogCategoryPreset p);
    static CAMEL_LOG_API LogCategoryPreset GetLogCategoryPreset();

    /// Non-empty: scope must match one entry by equality or as a dotted prefix (entry followed by
    /// '.').
    static CAMEL_LOG_API void SetScopeAllowPrefixes(std::vector<std::string> prefixes);
    static CAMEL_LOG_API const std::vector<std::string> &GetScopeAllowPrefixes();

    static CAMEL_LOG_API size_t AddOutputStream(std::ostream *os);
    static CAMEL_LOG_API void RemoveOutputStream(size_t handle);
    static CAMEL_LOG_API void WriteToAllStreams(const std::string &message);
    static CAMEL_LOG_API void SetColorEnabled(bool enable);
    static CAMEL_LOG_API bool IsColorEnabled();

    static CAMEL_LOG_API bool ShouldEmit(LogLevel level, std::string_view scope);
    static CAMEL_LOG_API void
    EmitLine(LogLevel level, std::string_view scope, const std::string &message);

  private:
    static LogLevel globalThreshold_;
    static LogCategoryPreset categoryPreset_;
    static std::vector<std::string> scopeAllowPrefixes_;
    static bool colorEnabled_;
    static std::mutex logMutex_;
    using StreamEntry = std::pair<size_t, std::ostream *>;
    static std::vector<StreamEntry> outputStreams_;
    static size_t nextStreamHandle_;
};

namespace camel_log_detail {

template <typename... Args>
void LogEmit(LogLevel level, const char *scope, std::format_string<Args...> fmt, Args &&...args) {
    if (!Logger::ShouldEmit(level, scope))
        return;
    Logger::EmitLine(level, scope, std::format(fmt, std::forward<Args>(args)...));
}

} // namespace camel_log_detail

/// Place once per .cpp that uses \c CAMEL_LOG_* without the \c _S suffix (before first use).
#define CAMEL_LOG_SCOPE(SCOPE_STR)                                                                 \
    namespace {                                                                                    \
    static constexpr const char *kCamelLogScope = (SCOPE_STR);                                     \
    }

#define CAMEL_LOG_FATAL(...)                                                                       \
    ::camel_log_detail::LogEmit(::LogLevel::Fatal, kCamelLogScope, __VA_ARGS__)
#define CAMEL_LOG_WARN(...)                                                                        \
    ::camel_log_detail::LogEmit(::LogLevel::Warn, kCamelLogScope, __VA_ARGS__)
#define CAMEL_LOG_INFO(...)                                                                        \
    ::camel_log_detail::LogEmit(::LogLevel::Info, kCamelLogScope, __VA_ARGS__)

#define CAMEL_LOG_FATAL_S(SCOPE, ...)                                                              \
    ::camel_log_detail::LogEmit(::LogLevel::Fatal, (SCOPE), __VA_ARGS__)
#define CAMEL_LOG_WARN_S(SCOPE, ...)                                                               \
    ::camel_log_detail::LogEmit(::LogLevel::Warn, (SCOPE), __VA_ARGS__)
#define CAMEL_LOG_INFO_S(SCOPE, ...)                                                               \
    ::camel_log_detail::LogEmit(::LogLevel::Info, (SCOPE), __VA_ARGS__)

#ifndef NDEBUG
#define CAMEL_LOG_DEBUG(...)                                                                       \
    ::camel_log_detail::LogEmit(::LogLevel::Debug, kCamelLogScope, __VA_ARGS__)
#define CAMEL_LOG_TRACE(...)                                                                       \
    ::camel_log_detail::LogEmit(::LogLevel::Trace, kCamelLogScope, __VA_ARGS__)
#define CAMEL_LOG_DEBUG_S(SCOPE, ...)                                                              \
    ::camel_log_detail::LogEmit(::LogLevel::Debug, (SCOPE), __VA_ARGS__)
#define CAMEL_LOG_TRACE_S(SCOPE, ...)                                                              \
    ::camel_log_detail::LogEmit(::LogLevel::Trace, (SCOPE), __VA_ARGS__)
#else
#define CAMEL_LOG_DEBUG(...) ((void)0)
#define CAMEL_LOG_TRACE(...) ((void)0)
#define CAMEL_LOG_DEBUG_S(SCOPE, ...) ((void)0)
#define CAMEL_LOG_TRACE_S(SCOPE, ...) ((void)0)
#endif
