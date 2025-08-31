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
#include "utils/assert.h"

class Diagnostic : public CamelBaseException {
  public:
    enum class Severity { Error = 1, Warning = 2, Information = 3, Hint = 4 };

    struct Position {
        size_t line = 0;
        size_t character = 0;
    };

    struct CharRange {
        Position start;
        Position end;
    };

    struct TokenRange {
        size_t start = 0;
        size_t end = 0;
    };

    Diagnostic(Severity sev, const std::string &msg, TokenRange range)
        : CamelBaseException(msg), severity_(sev), rangeStoredAsTokenIndex_(true) {
        range_.start.line = range.start;
        range_.end.line = range.end;
    }
    Diagnostic(Severity sev, const std::string &msg, Position pos)
        : CamelBaseException(msg), severity_(sev) {
        rangeStoredAsTokenIndex_ = false;
        range_.start.line = pos.line;
        range_.start.character = pos.character;
        range_.end.line = pos.line;
        range_.end.character = pos.character + 1;
    }
    Diagnostic(
        Severity sev, const std::string &msg, antlr4::Token *startToken, antlr4::Token *endToken)
        : CamelBaseException(msg), severity_(sev) {
        rangeStoredAsTokenIndex_ = false;
        range_.start.line = startToken->getLine() - 1;
        range_.start.character = startToken->getCharPositionInLine();
        if (endToken) {
            range_.end.line = endToken->getLine() - 1;
            range_.end.character = endToken->getCharPositionInLine() + endToken->getText().length();
        } else {
            range_.end = range_.start;
        }
    }

    Diagnostic &fetchRange(const std::vector<antlr4::Token *> &tokens) {
        if (rangeStoredAsTokenIndex_) {
            size_t startIndex = range_.start.line;
            size_t endIndex = range_.end.line;
            range_ = getRange(tokens, startIndex, endIndex);
            rangeStoredAsTokenIndex_ = false;
        }
        return *this;
    }

    std::string what(bool json = false) const override {
        ASSERT(
            !rangeStoredAsTokenIndex_,
            "Diagnostic range is stored as index, call fetchRange() first");
        std::ostringstream oss;
        if (json) {
            oss << "{"
                << "\"range\": {"
                << "\"start\": {\"line\": " << range_.start.line
                << ", \"character\": " << range_.start.character << "}, "
                << "\"end\": {\"line\": " << range_.end.line
                << ", \"character\": " << range_.end.character << "}"
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
    CharRange range_;
    Severity severity_;
    bool rangeStoredAsTokenIndex_ = false;

    Position getPosition(const std::vector<antlr4::Token *> &tokens, size_t index) const {
        if (index < tokens.size()) {
            antlr4::Token *tok = tokens[index];
            return Position{
                static_cast<size_t>(tok->getLine() - 1),
                static_cast<size_t>(tok->getCharPositionInLine())};
        }
        return Position{0, 0}; // 默认第一行第一列
    }

    CharRange
    getRange(const std::vector<antlr4::Token *> &tokens, size_t startIndex, size_t endIndex) const {
        Position start = getPosition(tokens, startIndex);
        Position end = getPosition(tokens, endIndex);

        if (end.line < start.line || (end.line == start.line && end.character < start.character)) {
            end = start;
        }

        if (endIndex < tokens.size()) {
            antlr4::Token *tok = tokens[endIndex];
            end.character += tok->getText().length();
        }

        return CharRange{start, end};
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
    DiagnosticsExceededLimitException(
        Diagnostic::Severity sev, size_t limit, const Diagnostic &lastDiagnostic)
        : DiagnosticsLimitExceededException(
              "Too many " + Diagnostic::severityToString(sev) +
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
        : DiagnosticsLimitExceededException(
              "Total diagnostic limit exceeded: " + std::to_string(total), lastDiagnostic),
          total_limit_(total) {}

    size_t totalLimit() const { return total_limit_; }

  private:
    size_t total_limit_;
};

struct DiagnosticsConfig {
    int total_limit = -1; // -1 means no limit
    std::unordered_map<Diagnostic::Severity, int> per_severity_limits = {};
};

class Diagnostics {
  public:
    using Severity = Diagnostic::Severity;
    Diagnostics(DiagnosticsConfig config = {}) : config_(config), current_index_(0) {}

    void setLimit(Severity sev, int limit) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (limit < 0) {
            config_.per_severity_limits.erase(sev);
        } else {
            config_.per_severity_limits[sev] = limit;
        }
    }

    void setTotalLimit(int limit) {
        std::lock_guard<std::mutex> lock(mutex_);
        config_.total_limit = limit;
    }

    void add(const Diagnostic &diag) {
        std::lock_guard<std::mutex> lock(mutex_);

        Severity sev = diag.severity();

        if (config_.total_limit >= 0 &&
            diagnostics_.size() >= static_cast<size_t>(config_.total_limit)) {
            throw DiagnosticsExceededTotalLimitException(config_.total_limit, diag);
        }

        if (config_.per_severity_limits.count(sev)) {
            int limit = config_.per_severity_limits[sev];
            if (limit >= 0 && severity_counts_[sev] >= static_cast<size_t>(limit)) {
                throw DiagnosticsExceededLimitException(sev, limit, diag);
            }
        }

        diagnostics_.push_back(diag);
        severity_counts_[sev]++;
    }

    void emplace(Severity sev, const std::string &msg, size_t startIndex = 0, size_t endIndex = 0) {
        add(Diagnostic(sev, msg, Diagnostic::TokenRange{startIndex, endIndex}));
    }

    void emplace(Severity sev, const std::string &msg, Diagnostic::TokenRange range) {
        add(Diagnostic(sev, msg, range));
    }

    void emplace(Severity sev, const std::string &msg, Diagnostic::Position pos) {
        add(Diagnostic(sev, msg, pos));
    }

    void emplace(
        Severity sev, const std::string &msg, antlr4::Token *startToken, antlr4::Token *endToken) {
        add(Diagnostic(sev, msg, startToken, endToken));
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

    void dump(std::ostream &os, bool json = false) {
        while (!this->end()) {
            auto diagOpt = this->next();
            if (diagOpt.has_value()) {
                auto &diag = diagOpt.value();
                os << diag.what(json) << std::endl;
            }
        }
    }

  private:
    mutable std::mutex mutex_;

    std::vector<Diagnostic> diagnostics_;
    std::unordered_map<Severity, size_t> severity_counts_;
    DiagnosticsConfig config_;
    size_t current_index_;
};

using diagnostics_ptr_t = std::shared_ptr<Diagnostics>;