// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#pragma once

#include "camel/core/source/manager.h"
#include "camel/utils/assert.h"
#include "camel/utils/debug.h"
#include "diagnostics/arguments.h"
#include "diagnostics/builder.h"
#include "diagnostics/messages/index.h"
#include "diagnostics/range.h"
#include "diagnostics/render.h"

#include <deque>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace camel::core::error {

// ---- Diagnostic structure ----
struct Diagnostic {
    DiagType type{};
    uint32_t specific{};
    Severity severity{};
    bool persisted = false;
    /// A warning switched off on the command line (-Wno-<name>): it is not stored and must not
    /// be shown. Set by Diagnostics::add on the copy it hands back.
    bool suppressed = false;
    /// A warning made an error on the command line (-Werror, -Werror=<name>). The code keeps
    /// its warning severity bits, so the diagnostic is still the same catalog entry.
    bool promoted = false;

    std::string moduleName;
    std::string modulePath;
    std::string name;
    std::string message;
    std::string suggestion;
    /// Pipeline stage the diagnostic was raised in: "parse", "typecheck", "pass:<name>" (a
    /// rewrite or translation pass), "run" (a scheduling pass), or "internal". Filled from
    /// stageForDiagnostic() when the diagnostic is created.
    std::string stage;

    // range can be a traditional CharRange/TokenRange or a lazily resolved SpanId/OriginId.
    SourceRange range{};
    /// Secondary locations, in the order they were attached.
    std::vector<DiagLabel> related;
    /// More secondary locations existed than were kept (see Diagnostics::addRelatedToErrorsFrom).
    bool relatedElided = false;
    /// The changes the report asks for that a tool can apply without asking (see DiagFix). Empty
    /// where the compiler does not know the text to write; `suggestion` then carries the advice.
    std::vector<DiagFix> fixes;
    /// Additional remarks, shown as `note:` lines after the excerpt.
    std::vector<std::string> notes;
    camel::source::source_context_ptr_t
        sourceContext; // Resolves SpanId/OriginId into final line/column positions.

    Diagnostic &fetchRange(const RangeConverter &conv);
    /// Text in the process-wide format (see diagnostics/render.h). The overload taking the
    /// destination stream decides colours for that stream.
    std::string toText() const;
    std::string toText(const std::ostream &os) const;
    std::string toJson() const;
    uint32_t diagCode() const;

    static std::string hex8(uint32_t v);

  private:
    static std::string escapeJson(const std::string &s);
    static char hexNib(int v);
};

/// The file a diagnostic is about: its module path, or the file of its span / origin.
std::string diagnosticPath(const Diagnostic &d);
/// The module name shown for a diagnostic (the stem of `path` when it has none of its own).
std::string diagnosticModuleName(const Diagnostic &d, const std::string &path);
/// The line/column range of `range`, when it has one that can be resolved now.
std::optional<CharRange>
resolveSourceRange(const SourceRange &range, const camel::source::source_context_ptr_t &ctx);

// ---- Warning policy (command line: -Wno-<name>, -Werror, -Werror=<name>) ----
/// Process-wide. Names are diagnostic names without the type prefix, compared without regard to
/// case, '-' and '_' ("ignored-side-effect" names IgnoredSideEffect).
struct WarningPolicy {
    bool disableAll  = false;
    bool allAsErrors = false;
    std::vector<std::string> disabled;
    std::vector<std::string> asErrors;
    std::vector<std::string> notAsErrors;
    /// Warnings shown before the rest are only counted (-N / --max-warning).
    size_t maxShown = static_cast<size_t>(-1);
};
/// Counts one more warning about to be shown; false once the limit of the policy is passed.
/// The first refusal is the place to say that further warnings are not shown.
bool admitShownWarning();
/// Number of warnings admitShownWarning() refused so far.
size_t warningsNotShown();
void setWarningPolicy(WarningPolicy policy);
/// The key warning names are compared by: lower case, without the type prefix, '-' and '_'.
std::string normalizeDiagnosticName(std::string_view name);
/// Applies the policy to a diagnostic that was not added to a Diagnostics container (which
/// applies it itself): marks a disabled warning `suppressed`, promotes one made an error.
/// Returns false when the diagnostic is suppressed.
bool applyWarningPolicy(Diagnostic &d);

/// Names the pipeline stage that diagnostics raised from now on belong to ("" = none: the
/// stage then follows the diagnostic type). The pass driver sets it around each pass.
void setPipelineStage(std::string stage);
/// The stage a diagnostic of `type` raised now belongs to (see Diagnostic::stage).
std::string stageForDiagnostic(DiagType type);

// ---- Exception classes ----
/// A limit that stopped a diagnostic from being recorded. The exception carries the diagnostic it
/// stopped on, and `what()` says it in one line (severity, place, message): the limit is control
/// flow, and a handler that reports "a limit was reached" without saying which diagnostic reached
/// it has thrown the information away.
///
/// The diagnostic is stored by value. It is the last one added to a container, and the container
/// is not the exception's to keep: the handler may clear it, a module may be unloaded, or the
/// container may be gone by the time `what()` is read. A copy lives exactly as long as the
/// exception object travels, which is what a `what()` returning a pointer into it needs.
class DiagnosticsLimitExceededBaseException : public std::exception {
  public:
    explicit DiagnosticsLimitExceededBaseException(const Diagnostic &diagnostic);

    const char *what() const noexcept override { return text_.c_str(); }
    /// The diagnostic the limit stopped on.
    const Diagnostic &diagnostic() const { return diagnostic_; }

  private:
    Diagnostic diagnostic_;
    /// Rendered once, where the diagnostic is still complete: `what()` must not resolve anything
    /// later (its range may only resolve while the source context is alive).
    std::string text_;
};

class DiagnosticsLimitExceededException : public DiagnosticsLimitExceededBaseException {
  public:
    DiagnosticsLimitExceededException(Severity sev, size_t limit, const Diagnostic &diagnostic)
        : DiagnosticsLimitExceededBaseException(diagnostic), severity_(sev), limit_(limit) {}

    Severity severity() const { return severity_; }
    size_t limit() const { return limit_; }

  private:
    Severity severity_;
    size_t limit_;
};

class DiagnosticsTotalLimitExceededException : public DiagnosticsLimitExceededBaseException {
  public:
    DiagnosticsTotalLimitExceededException(size_t total, const Diagnostic &diagnostic)
        : DiagnosticsLimitExceededBaseException(diagnostic), total_limit_(total) {}

    size_t totalLimit() const { return total_limit_; }

  private:
    size_t total_limit_;
};

// ---- Configuration ----
/// How many errors one compilation collects before it gives up, when the caller names no other
/// number. A tool that compiles sources itself (the command line, the debugger) uses this one
/// number: the same program must not be reported differently by two of them, and a limit of its
/// own would make one of them stop where the other records a diagnostic and goes on.
inline constexpr int kDefaultMaxErrors = 20;

struct DiagsConfig {
    int total_limit                                       = -1; // -1 means no limit
    std::unordered_map<Severity, int> per_severity_limits = {};

    // Helper methods.
    bool hasTotalLimit() const { return total_limit > 0; }
    bool hasSeverityLimit(Severity sev) const {
        return per_severity_limits.find(sev) != per_severity_limits.end() &&
               per_severity_limits.at(sev) != -1;
    }
    int getSeverityLimit(Severity sev) const {
        auto it = per_severity_limits.find(sev);
        return (it != per_severity_limits.end()) ? it->second : -1;
    }
};

// ---- Diagnostics manager ----
class Diagnostics {
  public:
    Diagnostics(
        std::string modName, std::string modPath,
        camel::source::source_context_ptr_t sourceContext = nullptr)
        : moduleName_(std::move(modName)), modulePath_(std::move(modPath)),
          sourceContext_(std::move(sourceContext)) {}

    // Configuration.
    void setConfig(const DiagsConfig &config) { config_ = config; }
    const DiagsConfig &getConfig() const { return config_; }

    Diagnostic &add(Diagnostic &&d);
    void fetchAll(const TokenPositions &tokens);
    /// When wrapInArray is false, emit only the joined items (no brackets) for merging.
    void dump(std::ostream &os, bool json = false, bool wrapInArray = true) const;
    /// The identity of every diagnostic: its code and the line/column of its range and of each
    /// secondary location, and nothing that is prose. A snapshot taken over this text records what
    /// the front end made of a file without recording how the report about it is worded, so
    /// rewording a message, a help text or a note cannot move such a snapshot. Ranges are
    /// expected to be resolved already (`fetchAll`); one that is not is written as `?`.
    void dumpIdentity(std::ostream &os) const;
    void clear();

    const std::string &moduleName() const { return moduleName_; }
    /// Also renames the diagnostics recorded under the old name: a file is named by the
    /// module it declares, and its syntax errors are recorded before that name is known.
    void setModuleName(std::string name) {
        std::lock_guard<std::mutex> lock(mtx_);
        for (Diagnostic &d : storage_) {
            if (d.moduleName == moduleName_) {
                d.moduleName = name;
            }
        }
        moduleName_ = std::move(name);
    }
    const std::string &modulePath() const { return modulePath_; }
    camel::source::source_context_ptr_t sourceContext() const { return sourceContext_; }
    void setSourceContext(camel::source::source_context_ptr_t sourceContext) {
        sourceContext_ = std::move(sourceContext);
    }

    // Return a builder and set its diagCode (severity can be inferred from the code).
    template <typename DiagEnum> DiagnosticBuilder of(DiagEnum err);

    // Query functions.
    std::vector<const Diagnostic *> findByType(DiagType type) const;
    std::vector<const Diagnostic *> findBySeverity(Severity severity) const;
    std::vector<const Diagnostic *> findByTypeAndSeverity(DiagType type, Severity severity) const;

    // Severity shortcut functions.
    std::vector<const Diagnostic *> errors() const { return findBySeverity(Severity::Error); }
    std::vector<const Diagnostic *> warnings() const { return findBySeverity(Severity::Warn); }
    std::vector<const Diagnostic *> infos() const { return findBySeverity(Severity::Info); }
    std::vector<const Diagnostic *> hints() const { return findBySeverity(Severity::Hint); }

    // Type + severity shortcuts.
    std::vector<const Diagnostic *> errors(DiagType type) const {
        return findByTypeAndSeverity(type, Severity::Error);
    }
    std::vector<const Diagnostic *> warnings(DiagType type) const {
        return findByTypeAndSeverity(type, Severity::Warn);
    }
    std::vector<const Diagnostic *> infos(DiagType type) const {
        return findByTypeAndSeverity(type, Severity::Info);
    }
    std::vector<const Diagnostic *> hints(DiagType type) const {
        return findByTypeAndSeverity(type, Severity::Hint);
    }

    // Statistics.
    size_t count() const;
    /// Attaches `label` as a secondary location to every error recorded at index `from` or
    /// later. For context that is only known where the failing work was requested, e.g. the
    /// call that asked for the instantiation of a generic function in whose body an error was
    /// found. An error that already has `limit` secondary locations is marked as having more
    /// (relatedElided) instead of getting another one (0: no limit).
    void addRelatedToErrorsFrom(size_t from, const DiagLabel &label, size_t limit = 0);
    /// Gives every diagnostic recorded at index `from` or later the file its origin lies in,
    /// when that is not this module's file: the body of a generic function imported from
    /// another module is compiled by the importing module, and its errors are in the other
    /// file.
    void locateByOriginFrom(size_t from);
    size_t count(Severity severity) const;
    size_t count(DiagType type) const;
    std::unordered_map<Severity, size_t> countBySeverity() const;
    std::unordered_map<DiagType, size_t> countByType() const;
    bool hasErrors() const;

  private:
    std::string moduleName_;
    std::string modulePath_;
    // All diagnostics attached to this container share the same SourceContext.
    camel::source::source_context_ptr_t sourceContext_;
    mutable std::mutex mtx_;
    std::deque<Diagnostic> storage_;
    /// What add() returns for a warning the policy switched off; never part of storage_.
    Diagnostic discarded_;
    DiagsConfig config_;

    // Helper methods for limit checking (use storage_.back() after push_back; never the moved-from
    // argument).
    void checkLimits();
    size_t countBySeverityInternal(Severity severity) const; // No lock version
};

template <typename DiagEnum> DiagnosticBuilder Diagnostics::of(DiagEnum err) {
    return DiagnosticBuilder::of(err, this);
}

template <typename DiagEnum>
DiagnosticBuilder DiagnosticBuilder::of(DiagEnum err, Diagnostics *diag) {
    DiagnosticBuilder builder(diag);

    builder.type_          = diagTypeOf(err);
    uint32_t key           = static_cast<uint32_t>(err);
    builder.severity_      = extractSeverity(key);
    builder.specific_      = extractSpecific(key);
    DiagInfo info          = getDiagInfo(err);
    builder.name_          = to_string(builder.type_) + "::" + info.name;
    builder.rawMessage_    = info.message;
    builder.rawSuggestion_ = info.suggestion;
    builder.moduleName_    = diag ? diag->moduleName() : "";
    builder.modulePath_    = diag ? diag->modulePath() : "";

    return builder;
}

template <typename... Args> Diagnostic DiagnosticBuilder::commit(Args &&...args) {
    Diagnostic d;
    d.range    = range_;
    d.severity = severity_;
    d.type     = type_;
    d.specific = specific_;
    d.name     = name_;
    d.stage    = stageForDiagnostic(type_);
    d.related  = std::move(related_);
    d.notes    = std::move(notes_);
    d.fixes    = std::move(fixes_);
    DiagnosticArguments arguments;
    (arguments.add(std::forward<Args>(args)), ...);
    d.message    = formatDiagnosticMessage(rawMessage_, arguments);
    d.suggestion = formatDiagnosticMessage(rawSuggestion_, arguments);
    d.moduleName = moduleName_;
    d.modulePath = modulePath_;
    if (diagnostics_ && diagnostics_->sourceContext()) {
        d.sourceContext = diagnostics_->sourceContext();
        if (std::holds_alternative<std::monostate>(d.range)) {
            auto current = d.sourceContext->currentRuntimeOrigin();
            if (current != camel::source::kInvalidOriginId) {
                d.range = current;
            }
        }
    }

    // A language error is an expected diagnostic, in Debug as well as Release.
    // Keep it on the sink's normal path; internal invariants assert at their fault site.

    if (diagnostics_) {
        return diagnostics_->add(std::move(d));
    } else {
        return d;
    }
}

using diagnostics_ptr_t = std::shared_ptr<Diagnostics>;

} // namespace camel::core::error
