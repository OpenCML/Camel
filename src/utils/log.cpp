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

#include "camel/utils/log.h"

#include <algorithm>
#include <cctype>
#include <iostream>

LogLevel Logger::globalThreshold_         = LogLevel::Fatal;
LogCategoryPreset Logger::categoryPreset_ = LogCategoryPreset::None;
std::vector<std::string> Logger::scopeAllowPrefixes_;
bool Logger::colorEnabled_ = true;
std::mutex Logger::logMutex_;
std::vector<Logger::StreamEntry> Logger::outputStreams_;
size_t Logger::nextStreamHandle_ = 0;

void Logger::SetGlobalLogThreshold(LogLevel t) { globalThreshold_ = t; }

LogLevel Logger::GetGlobalLogThreshold() { return globalThreshold_; }

void Logger::SetLogCategoryPreset(LogCategoryPreset p) { categoryPreset_ = p; }

LogCategoryPreset Logger::GetLogCategoryPreset() { return categoryPreset_; }

void Logger::SetScopeAllowPrefixes(std::vector<std::string> prefixes) {
    scopeAllowPrefixes_ = std::move(prefixes);
}

const std::vector<std::string> &Logger::GetScopeAllowPrefixes() { return scopeAllowPrefixes_; }

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

void Logger::SetColorEnabled(bool enable) { colorEnabled_ = enable; }

bool Logger::IsColorEnabled() { return colorEnabled_; }

static const std::vector<std::string> &wallScopePrefixes() {
    static const std::vector<std::string> k{
        "GIR",
        "Context",
        "Module",
        "Diag",
        "Main",
        "Frame",
        "FramePool",
        "ExecMgr",
        "Pass",
        "PythonModule",
        "codegen",
        "camel-cpp",
        "GlobalsBuiltinModule",
        "JIT.Trampoline",
    };
    return k;
}

static const std::vector<std::string> &extraScopePrefixes() {
    static const std::vector<std::string> k{
        "GIR",
        "Context",
        "Module",
        "Diag",
        "Main",
        "Frame",
        "FramePool",
        "ExecMgr",
        "JIT",
        "FastVM",
        "Taskflow",
        "NodeVM",
        "InlinePass",
        "Topo",
        "GraphViz",
        "Profiler",
        "Type",
        "UnionType",
        "TupleType",
        "StructType",
        "ArrayType",
        "FunctionType",
        "FunctionMetaInfo",
        "BumpPtr",
        "GIR Builder",
        "GlobalsBuiltinModule",
        "BasicExec",
        "IOExec",
        "JIT.Debug",
        "JIT.Trampoline",
        "Pass",
        "PythonModule",
        "codegen",
        "camel-cpp",
        "GlobalsBuiltinModule",
        "JIT.Trampoline",
    };
    return k;
}

static bool
scopeMatchesAllowList(std::string_view scope, const std::vector<std::string> &prefixes) {
    if (prefixes.empty())
        return true;
    for (const std::string &p : prefixes) {
        if (scope == p)
            return true;
        if (scope.size() > p.size() && scope[p.size()] == '.' && scope.compare(0, p.size(), p) == 0)
            return true;
    }
    return false;
}

static std::vector<std::string> copyEffectiveScopePrefixes() {
    if (!Logger::GetScopeAllowPrefixes().empty())
        return Logger::GetScopeAllowPrefixes();
    switch (Logger::GetLogCategoryPreset()) {
    case LogCategoryPreset::Wall:
        return wallScopePrefixes();
    case LogCategoryPreset::Extra:
        return extraScopePrefixes();
    case LogCategoryPreset::None:
    default:
        return {};
    }
}

bool Logger::ShouldEmit(LogLevel level, std::string_view scope) {
    if (level == LogLevel::Off)
        return false;
    if (globalThreshold_ == LogLevel::Off)
        return false;
    if (static_cast<int>(level) < static_cast<int>(globalThreshold_))
        return false;
    return scopeMatchesAllowList(scope, copyEffectiveScopePrefixes());
}

static std::string levelToTag(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "\033[1;35mTRACE\033[0m";
    case LogLevel::Debug:
        return "\033[1;36mDEBUG\033[0m";
    case LogLevel::Warn:
        return "\033[1;33mWARN\033[0m";
    case LogLevel::Info:
        return "\033[1;32mINFO\033[0m";
    case LogLevel::Fatal:
        return "\033[1;31mFATAL\033[0m";
    default:
        return "UNKNOWN";
    }
}

static std::string levelToPlain(LogLevel level) {
    switch (level) {
    case LogLevel::Trace:
        return "TRACE";
    case LogLevel::Debug:
        return "DEBUG";
    case LogLevel::Warn:
        return "WARN";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Fatal:
        return "FATAL";
    default:
        return "UNKNOWN";
    }
}

void Logger::EmitLine(LogLevel level, std::string_view scope, const std::string &message) {
    std::string tag         = colorEnabled_ ? levelToTag(level) : levelToPlain(level);
    std::string fullMessage = std::format("[{}] <{}> {}", tag, scope, message);

    std::lock_guard<std::mutex> lock(logMutex_);
    for (const auto &e : outputStreams_) {
        if (e.second) {
            *e.second << fullMessage << '\n' << std::flush;
        }
    }
}
