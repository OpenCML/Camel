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
 * Updated: Sep. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include "../base.h"
#include "builder.h"
#include "messages/index.h"
#include "range.h"

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

    CharRange range{};
    TokenRange tokenRange{};

    Diagnostic &fetchRange(const RangeConverter &mgr);
    std::string toText() const;
    std::string toJson() const;
    uint32_t diagCode() const;

  private:
    static std::string hex8(uint32_t v);
    static std::string escapeJson(const std::string &s);
    static char hexNib(int v);
};

// ---- Exception classes ----
class DiagnosticsLimitExceededException : public CamelBaseException {
  public:
    DiagnosticsLimitExceededException(const std::string &msg, const Diagnostic &lastDiag)
        : CamelBaseException(msg), lastDiagnostic_(lastDiag) {}

    const Diagnostic &lastDiagnostic() const { return lastDiagnostic_; }

  private:
    Diagnostic lastDiagnostic_;
};

class DiagnosticsExceededLimitException : public DiagnosticsLimitExceededException {
  public:
    DiagnosticsExceededLimitException(Severity sev, size_t limit, const Diagnostic &lastDiagnostic)
        : DiagnosticsLimitExceededException(
              "Too many " + to_string(sev) +
                  " diagnostics exceeded limit: " + std::to_string(limit),
              lastDiagnostic),
          severity_(sev), limit_(limit) {}

    Severity severity() const { return severity_; }
    size_t limit() const { return limit_; }

  private:
    Severity severity_;
    size_t limit_;
};

class DiagnosticsExceededTotalLimitException : public DiagnosticsLimitExceededException {
  public:
    DiagnosticsExceededTotalLimitException(size_t total, const Diagnostic &lastDiagnostic)
        : DiagnosticsLimitExceededException(
              "Total diagnostic limit exceeded: " + std::to_string(total), lastDiagnostic),
          total_limit_(total) {}

    size_t totalLimit() const { return total_limit_; }

  private:
    size_t total_limit_;
};

// ---- Configuration ----
struct DiagnosticsConfig {
    int total_limit = -1; // -1 means no limit
    std::unordered_map<Severity, int> per_severity_limits = {};

    // Helper methods
    bool hasTotalLimit() const { return total_limit > 0; }
    bool hasSeverityLimit(Severity sev) const {
        return per_severity_limits.find(sev) != per_severity_limits.end() &&
               per_severity_limits.at(sev) > 0;
    }
    int getSeverityLimit(Severity sev) const {
        auto it = per_severity_limits.find(sev);
        return (it != per_severity_limits.end()) ? it->second : -1;
    }
};

// ---- Diagnostics manager ----
class Diagnostics {
  public:
    Diagnostics() = default;
    Diagnostics(std::string modName, std::string modPath = "")
        : moduleName_(std::move(modName)), modulePath_(std::move(modPath)) {}

    // Configuration
    void setConfig(const DiagnosticsConfig &config) { config_ = config; }
    const DiagnosticsConfig &getConfig() const { return config_; }

    Diagnostic &add(Diagnostic &&d);
    void setTokens(const std::vector<antlr4::Token *> *tokens);
    void fetchAll(); // Fetch char index ranges for all token index ranges
    void outputAll(std::ostream &os) const;
    std::string toJson() const;
    void clear();

    // Return a builder + Set the diagCode for builder (could infer severity from diagCode)
    template <typename DiagEnum> DiagnosticBuilder of(DiagEnum err);

    // Query functions
    std::vector<const Diagnostic *> findByType(DiagType type) const;
    std::vector<const Diagnostic *> findBySeverity(Severity severity) const;
    std::vector<const Diagnostic *> findByTypeAndSeverity(DiagType type, Severity severity) const;

    // Severity shortcut functions
    std::vector<const Diagnostic *> errors() const { return findBySeverity(Severity::Error); }
    std::vector<const Diagnostic *> warnings() const { return findBySeverity(Severity::Warning); }
    std::vector<const Diagnostic *> infos() const { return findBySeverity(Severity::Info); }
    std::vector<const Diagnostic *> hints() const { return findBySeverity(Severity::Hint); }

    // Type + Severity shortcuts
    std::vector<const Diagnostic *> errors(DiagType type) const {
        return findByTypeAndSeverity(type, Severity::Error);
    }
    std::vector<const Diagnostic *> warnings(DiagType type) const {
        return findByTypeAndSeverity(type, Severity::Warning);
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

  private:
    std::string moduleName_;
    std::string modulePath_;
    mutable std::mutex mtx_;
    std::deque<Diagnostic> storage_;
    RangeConverter rangeConv_;
    DiagnosticsConfig config_;

    // Helper methods for limit checking
    void checkLimits(const Diagnostic &d);
    size_t countBySeverityInternal(Severity severity) const; // No lock version
};

template <typename DiagEnum> DiagnosticBuilder Diagnostics::of(DiagEnum err) {
    DiagnosticBuilder builder(*this);

    static_assert(std::is_enum_v<DiagEnum>, "Must be an enum type");
    builder.specific_ = static_cast<uint32_t>(err);

    DiagInfo info = getDiagInfo(err);
    builder.type_ = diagTypeOf(err);
    builder.name_ = info.name;
    builder.rawMessage_ = info.message;
    builder.rawSuggestion_ = info.suggestion;

    return builder;
}

using diagnostics_ptr_t = std::shared_ptr<Diagnostics>;
