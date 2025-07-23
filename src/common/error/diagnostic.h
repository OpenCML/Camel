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
 * Created: Jul. 13, 2025
 * Updated: Jul. 13, 2025
 * Supported by: National Key Research and Development Program of China
 */

#pragma once

#include <antlr4-runtime/antlr4-runtime.h>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "base.h"

class Diagnostic : public CamelBaseException {
  public:
    enum class Severity { Error = 1, Warning = 2, Information = 3, Hint = 4 };

    struct Position {
        size_t line = 0;
        size_t character = 0;
    };

    struct Range {
        Position start;
        Position end;
    };

    Diagnostic(Severity sev, const std::string &msg, size_t startIndex, size_t endIndex)
        : CamelBaseException(msg), severity_(sev) {
        rangeStoredAsIndex_ = true;
        range_.start.line = startIndex;
        range_.start.character = endIndex;
    }
    Diagnostic(Severity sev, const std::string &msg, antlr4::Token *startToken, antlr4::Token *endToken)
        : CamelBaseException(msg), severity_(sev) {
        rangeStoredAsIndex_ = false;
        range_.start.line = startToken->getLine() - 1;
        range_.start.character = startToken->getCharPositionInLine();
    }

    Diagnostic &fetchRange(const std::vector<antlr4::Token *> &tokens) {
        if (rangeStoredAsIndex_) {
            size_t startIndex = range_.start.line;
            size_t endIndex = range_.end.line;
            range_ = getRange(tokens, startIndex, endIndex);
            rangeStoredAsIndex_ = false;
        }
        return *this;
    }

    std::string what(bool json = false) const override {
        ASSERT(!rangeStoredAsIndex_, "Diagnostic range is stored as index, call fetchRange() first");
        std::ostringstream oss;
        if (json) {
            oss << "{"
                << "\"range\": {"
                << "\"start\": {\"line\": " << range_.start.line << ", \"character\": " << range_.start.character
                << "}, "
                << "\"end\": {\"line\": " << range_.end.line << ", \"character\": " << range_.end.character << "}"
                << "}, "
                << "\"message\": \"" << escapeJson(message_) << "\", "
                << "\"severity\": " << static_cast<int>(severity_) << ", "
                << "\"source\": \"BuildSystem\""
                << "}";
        } else {
            oss << "[" << severityToString(severity_) << "] "
                << "line: " << range_.start.line + 1 << ", "
                << "char: " << range_.start.character + 1 << " : " << message_;
        }
        return oss.str();
    }

    Severity severity() const { return severity_; }

    static std::string severityToString(Severity sev) {
        switch (sev) {
        case Severity::Error:
            return "error";
        case Severity::Warning:
            return "warn";
        case Severity::Information:
            return "info";
        case Severity::Hint:
            return "hint";
        }
        return "unknown";
    }

  private:
    bool rangeStoredAsIndex_ = false;
    Range range_;
    Severity severity_;

    Position getPosition(const std::vector<antlr4::Token *> &tokens, size_t index) const {
        if (index < tokens.size()) {
            antlr4::Token *tok = tokens[index];
            return Position{static_cast<size_t>(tok->getLine() - 1), static_cast<size_t>(tok->getCharPositionInLine())};
        }
        return Position{0, 0}; // 默认第一行第一列
    }

    Range getRange(const std::vector<antlr4::Token *> &tokens, size_t startIndex, size_t endIndex) const {
        Position start = getPosition(tokens, startIndex);
        Position end = getPosition(tokens, endIndex);

        if (end.line < start.line || (end.line == start.line && end.character < start.character)) {
            end = start;
        }

        if (endIndex < tokens.size()) {
            antlr4::Token *tok = tokens[endIndex];
            end.character += tok->getText().length();
        }

        return Range{start, end};
    }
};

class DiagnosticsLimitExceededException : public CamelBaseException {
  public:
    DiagnosticsLimitExceededException(const std::string &message, const Diagnostic &lastDiagnostic)
        : CamelBaseException(message), lastDiagnostic_(lastDiagnostic) {}

    Diagnostic &lastDiagnostic() { return lastDiagnostic_; }

  private:
    Diagnostic lastDiagnostic_;
};

class DiagnosticsExceededLimitException : public DiagnosticsLimitExceededException {
  public:
    DiagnosticsExceededLimitException(Diagnostic::Severity sev, size_t limit, const Diagnostic &lastDiagnostic)
        : DiagnosticsLimitExceededException("Too many " + Diagnostic::severityToString(sev) +
                                                " diagnostics exceeded limit: " + std::to_string(limit),
                                            lastDiagnostic),
          severity_(sev), limit_(limit) {}

    Diagnostic::Severity severity() const { return severity_; }
    size_t limit() const { return limit_; }

  private:
    Diagnostic::Severity severity_;
    size_t limit_;
};

class DiagnosticsExceededTotalLimitException : public DiagnosticsLimitExceededException {
  public:
    DiagnosticsExceededTotalLimitException(size_t total, const Diagnostic &lastDiagnostic)
        : DiagnosticsLimitExceededException("Total diagnostic limit exceeded: " + std::to_string(total),
                                            lastDiagnostic),
          total_limit_(total) {}

    size_t totalLimit() const { return total_limit_; }

  private:
    size_t total_limit_;
};

class Diagnostics {
  public:
    using Severity = Diagnostic::Severity;
    Diagnostics(int totalLimit = -1, std::unordered_map<Severity, int> perSeverityLimits = {})
        : total_limit_(totalLimit), per_severity_limits_(std::move(perSeverityLimits)), current_index_(0) {}

    void setLimit(Severity sev, int limit) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (limit < 0) {
            per_severity_limits_.erase(sev);
        } else {
            per_severity_limits_[sev] = limit;
        }
    }

    void setTotalLimit(int limit) {
        std::lock_guard<std::mutex> lock(mutex_);
        total_limit_ = limit;
    }

    void add(const Diagnostic &diag) {
        std::lock_guard<std::mutex> lock(mutex_);

        Severity sev = diag.severity();

        if (total_limit_ >= 0 && diagnostics_.size() >= static_cast<size_t>(total_limit_)) {
            throw DiagnosticsExceededTotalLimitException(total_limit_, diag);
        }

        if (per_severity_limits_.count(sev)) {
            int limit = per_severity_limits_[sev];
            if (limit >= 0 && severity_counts_[sev] >= static_cast<size_t>(limit)) {
                throw DiagnosticsExceededLimitException(sev, limit, diag);
            }
        }

        diagnostics_.push_back(diag);
        severity_counts_[sev]++;
    }

    void emplace(Severity sev, const std::string &msg, size_t startIndex = 0, size_t endIndex = 0) {
        add(Diagnostic(sev, msg, startIndex, endIndex));
    }

    std::optional<Diagnostic> next() {
        std::lock_guard<std::mutex> lock(mutex_);

        if (current_index_ < diagnostics_.size()) {
            return diagnostics_[current_index_++];
        }
        return std::nullopt;
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        current_index_ = 0;
    }

    bool end() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return current_index_ >= diagnostics_.size();
    }

    std::vector<Diagnostic> get_by_severity(Severity severity) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<Diagnostic> result;
        for (const auto &diag : diagnostics_) {
            if (diag.severity() == severity) {
                result.push_back(diag);
            }
        }
        return result;
    }

    std::vector<Diagnostic> errors() { return get_by_severity(Severity::Error); }
    std::vector<Diagnostic> warnings() { return get_by_severity(Severity::Warning); }
    std::vector<Diagnostic> information() { return get_by_severity(Severity::Information); }
    std::vector<Diagnostic> hints() { return get_by_severity(Severity::Hint); }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return diagnostics_.size();
    }

    bool hasErrors() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return severity_counts_.count(Severity::Error) && severity_counts_.at(Severity::Error) > 0;
    }

  private:
    mutable std::mutex mutex_;

    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<Severity, size_t> severity_counts_;
    int total_limit_;
    std::unordered_map<Severity, int> per_severity_limits_;
    size_t current_index_;
};

using diagnostics_ptr_t = std::shared_ptr<Diagnostics>;