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
 * Created: Mar. 07, 2026
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "camel/core/error/diagnostics.h"
#include "camel/core/error/diagnostics/messages/runtime.h"

#include <cstdint>
#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

class Context;

namespace GraphIR {
class Graph;
class Node;
} // namespace GraphIR

enum class RuntimeDiagMode : uint8_t {
    OnErrorOnly,
    DebuggerAttached,
};

enum class RuntimeFaultPolicy : uint8_t {
    AbortCurrentExecution,
    AbortCurrentTask,
    FatalProcess,
};

enum class ExecutionSiteKind : uint8_t {
    Node,
    BytecodePc,
    TaskNode,
    JitPc,
    Unknown,
};

struct ExecutionSiteHint {
    ExecutionSiteKind kind                  = ExecutionSiteKind::Unknown;
    GraphIR::Graph *graph                   = nullptr;
    GraphIR::Node *node                     = nullptr;
    size_t pc                               = 0;
    camel::source::origin_id_t cachedOrigin = camel::source::kInvalidOriginId;
    std::string stableId;
};

struct ExecutionSite {
    ExecutionSiteKind kind                  = ExecutionSiteKind::Unknown;
    GraphIR::Graph *graph                   = nullptr;
    GraphIR::Node *node                     = nullptr;
    size_t pc                               = 0;
    camel::source::origin_id_t cachedOrigin = camel::source::kInvalidOriginId;
    std::string stableId;
    std::thread::id threadId = std::this_thread::get_id();
    size_t frameDepth        = 0;
    std::string taskLabel;
};

struct ResolvedRuntimeLocation {
    camel::source::origin_id_t origin = camel::source::kInvalidOriginId;
    bool hasRange                     = false;
    CharRange range{};
    std::string sourcePath;
};

class DiagnosticSink : public Diagnostics {
  public:
    using Diagnostics::Diagnostics;
};

using diagnostic_sink_ptr_t        = std::shared_ptr<DiagnosticSink>;
using runtime_error_reporter_ptr_t = std::shared_ptr<class RuntimeErrorReporter>;

namespace runtime_fault_detail {

inline std::string argToString(const std::string &value) { return value; }
inline std::string argToString(std::string &&value) { return std::move(value); }
inline std::string argToString(const char *value) {
    return value ? std::string(value) : std::string();
}
inline std::string argToString(char *value) { return value ? std::string(value) : std::string(); }
inline std::string argToString(bool value) { return value ? "true" : "false"; }

template <typename T> std::string argToString(const T &value) {
    std::ostringstream os;
    os << value;
    return os.str();
}

} // namespace runtime_fault_detail

class RuntimeFault : public std::exception {
  public:
    RuntimeFault(
        RuntimeDiag diag, std::vector<std::string> formatArgs = {}, std::string detail = "",
        std::exception_ptr cause = nullptr, ExecutionSiteHint hint = {},
        RuntimeFaultPolicy policy = RuntimeFaultPolicy::AbortCurrentExecution);

    template <typename... Args> static RuntimeFault make(RuntimeDiag diag, Args &&...args) {
        std::vector<std::string> formatArgs;
        formatArgs.reserve(sizeof...(Args));
        (formatArgs.push_back(runtime_fault_detail::argToString(std::forward<Args>(args))), ...);
        return RuntimeFault(diag, std::move(formatArgs));
    }

    RuntimeDiag diag() const { return diag_; }
    const std::vector<std::string> &formatArgs() const { return formatArgs_; }
    const std::string &detail() const { return detail_; }
    std::exception_ptr cause() const { return cause_; }
    const ExecutionSiteHint &hint() const { return hint_; }
    RuntimeFaultPolicy policy() const { return policy_; }

    const char *what() const noexcept override;

  private:
    RuntimeDiag diag_;
    std::vector<std::string> formatArgs_;
    std::string detail_;
    std::exception_ptr cause_;
    ExecutionSiteHint hint_;
    RuntimeFaultPolicy policy_;
    mutable std::string what_;
};

class RuntimeLocationResolver {
  public:
    explicit RuntimeLocationResolver(camel::source::source_context_ptr_t sourceContext)
        : sourceContext_(std::move(sourceContext)) {}

    ResolvedRuntimeLocation resolve(const ExecutionSite &site) const;

  private:
    camel::source::source_context_ptr_t sourceContext_;
};

class RuntimeErrorReporter {
  public:
    RuntimeErrorReporter(
        diagnostic_sink_ptr_t sink, camel::source::source_context_ptr_t sourceContext)
        : sink_(std::move(sink)), resolver_(std::move(sourceContext)) {}

    Diagnostic report(const RuntimeFault &fault, const ExecutionSite &site) const;
    diagnostic_sink_ptr_t sink() const { return sink_; }

  private:
    diagnostic_sink_ptr_t sink_;
    RuntimeLocationResolver resolver_;
};

ExecutionSite makeGraphExecutionSite(
    camel::source::source_context_ptr_t sourceContext, GraphIR::Graph *graph, size_t frameDepth = 0,
    std::string taskLabel = "");
ExecutionSite makeNodeExecutionSite(
    camel::source::source_context_ptr_t sourceContext, GraphIR::Graph *graph, GraphIR::Node *node,
    size_t frameDepth = 0, std::string taskLabel = "",
    ExecutionSiteKind kind = ExecutionSiteKind::Node);
ExecutionSite makePcExecutionSite(
    camel::source::source_context_ptr_t sourceContext, GraphIR::Graph *graph, size_t pc,
    size_t frameDepth = 0, std::string taskLabel = "",
    ExecutionSiteKind kind = ExecutionSiteKind::BytecodePc);

Diagnostic reportRuntimeFault(Context &ctx, const RuntimeFault &fault, const ExecutionSite &site);

template <typename... Args>
[[noreturn]] inline void throwRuntimeFault(RuntimeDiag diag, Args &&...args) {
    throw RuntimeFault::make(diag, std::forward<Args>(args)...);
}
