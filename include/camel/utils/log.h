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

#include <concepts>
#include <cstdint>
#include <format>
#include <functional>
#include <mutex>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "camel/utils/debug.h"

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

namespace camel::log {

/// Runs \p fn only if a line at \p level would be emitted for \p scope. Use when the message is
/// expensive to build (directory scans, large graphs). \p fn must return \c std::string (e.g.
/// \c std::format inside the lambda). For plain \c std::format with cheap arguments, prefer
/// \c CAMEL_LOG_*_S (those skip \c std::format until after \c ShouldEmit).
template <typename F>
    requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, std::string>
inline void emit_lazy(LogLevel level, std::string_view scope, F &&fn) {
    if (!Logger::ShouldEmit(level, scope))
        return;
    Logger::EmitLine(level, scope, std::invoke(std::forward<F>(fn)));
}

template <typename F>
    requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, std::string>
inline void fatal_lazy(std::string_view scope, F &&fn) {
    emit_lazy(LogLevel::Fatal, scope, std::forward<F>(fn));
}

template <typename F>
    requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, std::string>
inline void warn_lazy(std::string_view scope, F &&fn) {
    emit_lazy(LogLevel::Warn, scope, std::forward<F>(fn));
}

template <typename F>
    requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, std::string>
inline void info_lazy(std::string_view scope, F &&fn) {
    emit_lazy(LogLevel::Info, scope, std::forward<F>(fn));
}

#ifndef NDEBUG
template <typename F>
    requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, std::string>
inline void debug_lazy(std::string_view scope, F &&fn) {
    emit_lazy(LogLevel::Debug, scope, std::forward<F>(fn));
}

template <typename F>
    requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, std::string>
inline void trace_lazy(std::string_view scope, F &&fn) {
    emit_lazy(LogLevel::Trace, scope, std::forward<F>(fn));
}
#endif

} // namespace camel::log

/// Emits one headline (from \p headline_fn) at Info, then numbered non-empty \p paths.
/// \p headline_fn runs only when Info is enabled for \p scope (cheap when logging is off).
template <typename F>
    requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, std::string>
inline void
LogInfoPathList(std::string_view scope, F &&headline_fn, const std::vector<std::string> &paths) {
    if (!Logger::ShouldEmit(LogLevel::Info, scope))
        return;
    Logger::EmitLine(LogLevel::Info, scope, std::forward<F>(headline_fn)());
    std::size_t idx = 0;
    for (const auto &p : paths) {
        if (p.empty())
            continue;
        ++idx;
        Logger::EmitLine(LogLevel::Info, scope, std::format("{:>4}. {}", idx, p));
    }
}

/// Place once per .cpp that uses \c CAMEL_LOG_* without the \c _S suffix (before first use).
#define CAMEL_LOG_SCOPE(SCOPE_STR)                                                                 \
    namespace {                                                                                    \
    static constexpr const char *kCamelLogScope = (SCOPE_STR);                                     \
    }

/// \c ShouldEmit is checked before \c std::format so format arguments are not evaluated when
/// logging is disabled (or scope-filtered).
#define CAMEL_LOG_FATAL(...)                                                                       \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Fatal, kCamelLogScope))                              \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Fatal, kCamelLogScope, std::format(__VA_ARGS__));           \
    } while (0)
#define CAMEL_LOG_WARN(...)                                                                        \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Warn, kCamelLogScope))                               \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Warn, kCamelLogScope, std::format(__VA_ARGS__));            \
    } while (0)
#define CAMEL_LOG_INFO(...)                                                                        \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Info, kCamelLogScope))                               \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Info, kCamelLogScope, std::format(__VA_ARGS__));            \
    } while (0)

#define CAMEL_LOG_FATAL_S(SCOPE, ...)                                                              \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Fatal, (SCOPE)))                                     \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Fatal, (SCOPE), std::format(__VA_ARGS__));                  \
    } while (0)
#define CAMEL_LOG_WARN_S(SCOPE, ...)                                                               \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Warn, (SCOPE)))                                      \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Warn, (SCOPE), std::format(__VA_ARGS__));                   \
    } while (0)
#define CAMEL_LOG_INFO_S(SCOPE, ...)                                                               \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Info, (SCOPE)))                                      \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Info, (SCOPE), std::format(__VA_ARGS__));                   \
    } while (0)

#ifndef NDEBUG
#define CAMEL_LOG_DEBUG(...)                                                                       \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Debug, kCamelLogScope))                              \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Debug, kCamelLogScope, std::format(__VA_ARGS__));           \
    } while (0)
#define CAMEL_LOG_TRACE(...)                                                                       \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Trace, kCamelLogScope))                              \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Trace, kCamelLogScope, std::format(__VA_ARGS__));           \
    } while (0)
#define CAMEL_LOG_DEBUG_S(SCOPE, ...)                                                              \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Debug, (SCOPE)))                                     \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Debug, (SCOPE), std::format(__VA_ARGS__));                  \
    } while (0)
#define CAMEL_LOG_TRACE_S(SCOPE, ...)                                                              \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Trace, (SCOPE)))                                     \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Trace, (SCOPE), std::format(__VA_ARGS__));                  \
    } while (0)
#else
#define CAMEL_LOG_DEBUG(...) ((void)0)
#define CAMEL_LOG_TRACE(...) ((void)0)
#define CAMEL_LOG_DEBUG_S(SCOPE, ...) ((void)0)
#define CAMEL_LOG_TRACE_S(SCOPE, ...) ((void)0)
#endif
