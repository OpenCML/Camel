// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include <concepts>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "camel/utils/debug.h"
#include "camel/utils/log_format.h"

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
/// \c camel::log::formatLog inside the lambda). For a plain \c CAMEL_LOG_*_S with cheap arguments,
/// prefer the macro (it skips building the line until after \c ShouldEmit).
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
        Logger::EmitLine(LogLevel::Info, scope, camel::log::formatLog("{:>4}. {}", idx, p));
    }
}

/// Place once per .cpp that uses \c CAMEL_LOG_* without the \c _S suffix (before first use).
#define CAMEL_LOG_SCOPE(SCOPE_STR)                                                                 \
    namespace {                                                                                    \
    static constexpr const char *kCamelLogScope = (SCOPE_STR);                                     \
    }

/// \c ShouldEmit is checked before the line is built, so a format argument is not evaluated when
/// logging is disabled (or scope-filtered). The macros without a scope use the one \c
/// CAMEL_LOG_SCOPE declared.
#define CAMEL_LOG_FATAL(...) CAMEL_LOG_FATAL_S(kCamelLogScope, __VA_ARGS__)
#define CAMEL_LOG_WARN(...) CAMEL_LOG_WARN_S(kCamelLogScope, __VA_ARGS__)
#define CAMEL_LOG_INFO(...) CAMEL_LOG_INFO_S(kCamelLogScope, __VA_ARGS__)

#define CAMEL_LOG_FATAL_S(SCOPE, ...)                                                              \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Fatal, (SCOPE)))                                     \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Fatal, (SCOPE), ::camel::log::formatLog(__VA_ARGS__));      \
    } while (0)
#define CAMEL_LOG_WARN_S(SCOPE, ...)                                                               \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Warn, (SCOPE)))                                      \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Warn, (SCOPE), ::camel::log::formatLog(__VA_ARGS__));       \
    } while (0)
#define CAMEL_LOG_INFO_S(SCOPE, ...)                                                               \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Info, (SCOPE)))                                      \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Info, (SCOPE), ::camel::log::formatLog(__VA_ARGS__));       \
    } while (0)

#ifndef NDEBUG
#define CAMEL_LOG_DEBUG(...) CAMEL_LOG_DEBUG_S(kCamelLogScope, __VA_ARGS__)
#define CAMEL_LOG_TRACE(...) CAMEL_LOG_TRACE_S(kCamelLogScope, __VA_ARGS__)
#define CAMEL_LOG_DEBUG_S(SCOPE, ...)                                                              \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Debug, (SCOPE)))                                     \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Debug, (SCOPE), ::camel::log::formatLog(__VA_ARGS__));      \
    } while (0)
#define CAMEL_LOG_TRACE_S(SCOPE, ...)                                                              \
    do {                                                                                           \
        if (!::Logger::ShouldEmit(::LogLevel::Trace, (SCOPE)))                                     \
            break;                                                                                 \
        ::Logger::EmitLine(::LogLevel::Trace, (SCOPE), ::camel::log::formatLog(__VA_ARGS__));      \
    } while (0)
#else
#define CAMEL_LOG_DEBUG(...) ((void)0)
#define CAMEL_LOG_TRACE(...) ((void)0)
#define CAMEL_LOG_DEBUG_S(SCOPE, ...) ((void)0)
#define CAMEL_LOG_TRACE_S(SCOPE, ...) ((void)0)
#endif
