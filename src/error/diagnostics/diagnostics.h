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
 * Created: Sep. 06, 2025
 * Updated: Oct. 31, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../base.h"
#include "builder.h"
#include "messages/index.h"
#include "range.h"

#include "fmt/core.h"

// ---- Diagnostic structure ----
struct Diagnostic {
    DiagType type{};
    uint32_t specific{};
    Severity severity{};

    std::string moduleName;
    std::string modulePath;
    std::string name;
    std::string message;
    std::string suggestion;

    SourceRange range{};

    Diagnostic &fetchRange(const RangeConverter &conv);
    std::string toText() const;
    std::string toJson() const;
    uint32_t diagCode() const;

  private:
    static std::string hex8(uint32_t v);
    static std::string escapeJson(const std::string &s);
    static char hexNib(int v);
};

// ---- Exception classes ----
class DiagnosticsLimitExceededBaseException : public CamelBaseException {
  public:
    DiagnosticsLimitExceededBaseException(const std::string &msg) : CamelBaseException(msg) {}
};

class DiagnosticsLimitExceededException : public DiagnosticsLimitExceededBaseException {
  public:
    DiagnosticsLimitExceededException(Severity sev, size_t limit)
        : DiagnosticsLimitExceededBaseException(
              "Too many " + to_string(sev) +
              " diagnostics exceeded limit: " + std::to_string(limit)),
          severity_(sev), limit_(limit) {}

    Severity severity() const { return severity_; }
    size_t limit() const { return limit_; }

  private:
    Severity severity_;
    size_t limit_;
};

class DiagnosticsTotalLimitExceededException : public DiagnosticsLimitExceededBaseException {
  public:
    DiagnosticsTotalLimitExceededException(size_t total)
        : DiagnosticsLimitExceededBaseException(
              "Total diagnostic limit exceeded: " + std::to_string(total)),
          total_limit_(total) {}

    size_t totalLimit() const { return total_limit_; }

  private:
    size_t total_limit_;
};

// ---- Configuration ----
struct DiagsConfig {
    int total_limit = -1; // -1 means no limit
    std::unordered_map<Severity, int> per_severity_limits = {};

    // Helper methods
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
    Diagnostics(std::string modName, std::string modPath)
        : moduleName_(std::move(modName)), modulePath_(std::move(modPath)) {}

    // Configuration
    void setConfig(const DiagsConfig &config) { config_ = config; }
    const DiagsConfig &getConfig() const { return config_; }

    Diagnostic &add(Diagnostic &&d);
    void fetchAll(const std::vector<antlr4::Token *> &tokens);
    void dump(std::ostream &os, bool json = false) const;
    void clear();

    const std::string &moduleName() const { return moduleName_; }
    const std::string &modulePath() const { return modulePath_; }

    // Return a builder + Set the diagCode for builder (could infer severity from diagCode)
    template <typename DiagEnum> DiagnosticBuilder of(DiagEnum err);

    // Query functions
    std::vector<const Diagnostic *> findByType(DiagType type) const;
    std::vector<const Diagnostic *> findBySeverity(Severity severity) const;
    std::vector<const Diagnostic *> findByTypeAndSeverity(DiagType type, Severity severity) const;

    // Severity shortcut functions
    std::vector<const Diagnostic *> errors() const { return findBySeverity(Severity::Error); }
    std::vector<const Diagnostic *> warnings() const { return findBySeverity(Severity::Warn); }
    std::vector<const Diagnostic *> infos() const { return findBySeverity(Severity::Info); }
    std::vector<const Diagnostic *> hints() const { return findBySeverity(Severity::Hint); }

    // Type + Severity shortcuts
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

    // Statistics
    size_t count() const;
    size_t count(Severity severity) const;
    size_t count(DiagType type) const;
    std::unordered_map<Severity, size_t> countBySeverity() const;
    std::unordered_map<DiagType, size_t> countByType() const;
    bool hasErrors() const;

  private:
    std::string moduleName_;
    std::string modulePath_;
    mutable std::mutex mtx_;
    std::deque<Diagnostic> storage_;
    DiagsConfig config_;

    // Helper methods for limit checking
    void checkLimits(const Diagnostic &d);
    size_t countBySeverityInternal(Severity severity) const; // No lock version
};

template <typename DiagEnum> DiagnosticBuilder Diagnostics::of(DiagEnum err) {
    return DiagnosticBuilder::of(err, this);
}

template <typename DiagEnum>
DiagnosticBuilder DiagnosticBuilder::of(DiagEnum err, Diagnostics *diag) {
    DiagnosticBuilder builder(diag);

    builder.type_ = diagTypeOf(err);
    uint32_t key = static_cast<uint32_t>(err);
    builder.severity_ = extractSeverity(key);
    builder.specific_ = extractSpecific(key);
    DiagInfo info = getDiagInfo(err);
    builder.name_ = to_string(builder.type_) + "::" + info.name;
    builder.rawMessage_ = info.message;
    builder.rawSuggestion_ = info.suggestion;
    builder.moduleName_ = diag ? diag->moduleName() : "";
    builder.modulePath_ = diag ? diag->modulePath() : "";

    return builder;
}

template <typename... Args> Diagnostic DiagnosticBuilder::commit(Args &&...args) {
    Diagnostic d;
    d.range = range_;
    d.severity = severity_;
    d.type = type_;
    d.specific = specific_;
    d.name = name_;
    try {
        d.message = fmt::format(fmt::runtime(rawMessage_), std::forward<Args>(args)...);
        d.suggestion = fmt::format(fmt::runtime(rawSuggestion_), std::forward<Args>(args)...);
    } catch (const fmt::format_error &e) {
        throw DiagnosticBuilder::of(InternalDiag::UnknownInternalError)
            .commit("Error formatting diagnostic message: " + std::string(e.what()));
    }
    d.moduleName = moduleName_;
    d.modulePath = modulePath_;

    if (diagnostics_) {
        return diagnostics_->add(std::move(d));
    } else {
        return d;
    }
}

using diagnostics_ptr_t = std::shared_ptr<Diagnostics>;
