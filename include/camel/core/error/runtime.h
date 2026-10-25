// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

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

namespace camel::core::context {
class Context;
class Frame;
} // namespace camel::core::context
namespace camel::runtime {
class GCGraph;
}

namespace camel::core::error {

/// The message of `diag` with `args` put in place of `{0}`, `{1}`, ... the sentence a
/// diagnostic reports, without its position or help. For a report nested inside another — the
/// reason an entry argument was refused says what made it unreadable (design principles 10.6).
std::string runtimeDiagMessage(RuntimeDiag diag, const std::vector<std::string> &args);

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
    camel::runtime::GCGraph *runtimeGraph   = nullptr;
    size_t pc                               = 0;
    camel::source::origin_id_t cachedOrigin = camel::source::kInvalidOriginId;
};

struct ExecutionSite {
    ExecutionSiteKind kind                  = ExecutionSiteKind::Unknown;
    camel::runtime::GCGraph *runtimeGraph   = nullptr;
    size_t pc                               = 0;
    camel::source::origin_id_t cachedOrigin = camel::source::kInvalidOriginId;
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

/// A query of a declaration with parameters named parts that are no value's operands (in either
/// order a commutative operator allows). It is not a fault of the program: the rule of
/// 2026-10-23 on `rule_when_const_node` makes the condition that reads such a query false -- the
/// rule does not apply here, the pass does not fail ("条件里的事实查询找不到时，条件为假（这条规则在
/// 此不适用），而不是遍失败——条件本来就是"适用与否"的判断"). Whoever evaluates a condition catches
/// it; a run that does not is a defect.
class FactQueryNoValue : public std::exception {
  public:
    explicit FactQueryNoValue(std::string what) : what_(std::move(what)) {}
    const char *what() const noexcept override { return what_.c_str(); }

  private:
    std::string what_;
};

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

    /// Records the fault in the sink and returns the recorded diagnostic (or throws, when the
    /// sink stops at the first error).
    Diagnostic report(const RuntimeFault &fault, const ExecutionSite &site) const;
    /// The same located diagnostic, not recorded anywhere (`persisted` stays false): what a
    /// fault inside a fault-isolation boundary is carried to the boundary as.
    Diagnostic describe(const RuntimeFault &fault, const ExecutionSite &site) const;
    diagnostic_sink_ptr_t sink() const { return sink_; }

  private:
    diagnostic_sink_ptr_t sink_;
    RuntimeLocationResolver resolver_;
};

ExecutionSite makeGraphExecutionSite(
    camel::source::source_context_ptr_t sourceContext, camel::runtime::GCGraph *graph,
    size_t frameDepth = 0, std::string taskLabel = "");
ExecutionSite makePcExecutionSite(
    camel::source::source_context_ptr_t sourceContext, const camel::core::context::Frame *frame,
    size_t pc, size_t frameDepth = 0, std::string taskLabel = "",
    ExecutionSiteKind kind = ExecutionSiteKind::BytecodePc);

/// Reports a fault at `site`. Outside every fault-isolation boundary the diagnostic is recorded
/// in the context's sink; inside one (FaultIsolationScope) a fault that can be isolated is only
/// described: the diagnostic returned was not recorded, and whoever catches it at the boundary
/// turns it into an error value. Inside a DescribedFaultScope nothing is recorded either.
/// Either way the caller throws what is returned.
Diagnostic reportRuntimeFault(
    camel::core::context::Context &ctx, const RuntimeFault &fault, const ExecutionSite &site);

// ---- Fault isolation (design principles 3.9; docs/technical/33_fault_isolation.md) ----

/// Whether a fault with this diagnostic may become an error value at a fault-isolation
/// boundary. An allow-list of the faults of a program's own computation: defects and limits of
/// the compiler (internal errors, unknown operators, graph capacity, memory) are never isolated,
/// and a diagnostic added later is not isolated until it is listed here.
/// DESIGN-QUESTION(attempt-fault-set)
bool isIsolatableFault(RuntimeDiag diag);

/// Whether `d` is a fault on its way to a fault-isolation boundary: described by
/// reportRuntimeFault inside a boundary, so never recorded, and of an isolatable kind.
bool isIsolatedFault(const Diagnostic &d);

/// Number of fault-isolation boundaries open around what the calling thread is running.
uint32_t faultIsolationDepth();

/// Opens one boundary on the calling thread for its lifetime.
class FaultIsolationScope {
  public:
    FaultIsolationScope();
    ~FaultIsolationScope();
    FaultIsolationScope(const FaultIsolationScope &)            = delete;
    FaultIsolationScope &operator=(const FaultIsolationScope &) = delete;
};

/// A task that runs on another thread on behalf of code inside boundaries: while the object
/// lives the calling thread counts `depth` open boundaries, afterwards what it had.
class InheritedFaultIsolation {
  public:
    explicit InheritedFaultIsolation(uint32_t depth);
    ~InheritedFaultIsolation();
    InheritedFaultIsolation(const InheritedFaultIsolation &)            = delete;
    InheritedFaultIsolation &operator=(const InheritedFaultIsolation &) = delete;

  private:
    uint32_t saved_;
};

/// Opened by a scheduler that decides itself which fault is recorded (the parallel scheduler:
/// of the faults of elements that ran concurrently, only the one of the smallest index is the
/// fault of the run). While one is open on the calling thread, reportRuntimeFault describes
/// every fault and records none, whatever interpreter raises it; the scheduler records the
/// diagnostic it lets through. Scopes nest; a task on another thread opens its own.
class DescribedFaultScope {
  public:
    DescribedFaultScope();
    ~DescribedFaultScope();
    DescribedFaultScope(const DescribedFaultScope &)            = delete;
    DescribedFaultScope &operator=(const DescribedFaultScope &) = delete;
};

template <typename... Args>
[[noreturn]] inline void throwRuntimeFault(RuntimeDiag diag, Args &&...args) {
    throw RuntimeFault::make(diag, std::forward<Args>(args)...);
}

} // namespace camel::core::error
