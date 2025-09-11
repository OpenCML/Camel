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

#include "diagnostic.h"

// ---- SourceRangeManager implementation ----
void SourceRangeManager::setTokens(const std::vector<antlr4::Token *> *toks) {
    tokenPassed = true;
    tokens_ = toks;
}

ErrorRange SourceRangeManager::fromTokenIndices(size_t startIdx, size_t endIdx) const {
    ErrorRange r{{0, 0}, {0, 0}};
    if (!tokens_ || tokens_->empty())
        return r;

    auto conv = [&](size_t i) -> ErrorPosition {
        if (i < tokens_->size()) {
            auto *t = (*tokens_)[i];
            return {
                static_cast<size_t>(t->getLine() - 1),
                static_cast<size_t>(t->getCharPositionInLine())};
        }
        return ErrorPosition{0, 0};
    };

    ErrorPosition s = conv(startIdx);
    ErrorPosition e = conv(endIdx);
    if (e.line < s.line || (e.line == s.line && e.character < s.character))
        e = s;
    else if (endIdx < tokens_->size()) {
        auto *t = (*tokens_)[endIdx];
        e.character += t->getText().length();
    }
    r.start = s;
    r.end = e;
    return r;
}

ErrorRange SourceRangeManager::fromTokenPointers(antlr4::Token *start, antlr4::Token *end) const {
    ErrorRange r{{0, 0}, {0, 0}};
    if (start) {
        r.start.line = static_cast<size_t>(start->getLine() - 1);
        r.start.character = static_cast<size_t>(start->getCharPositionInLine());
    }
    if (end) {
        r.end.line = static_cast<size_t>(end->getLine() - 1);
        r.end.character =
            static_cast<size_t>(end->getCharPositionInLine() + end->getText().length());
    } else {
        r.end = r.start;
    }
    return r;
}

// ---- Diagnostic implementation ----
Diagnostic &Diagnostic::fetchRange(const SourceRangeManager &mgr) {
    range = mgr.fromTokenIndices(tokenRange.start, tokenRange.end);
    return *this;
}

std::string Diagnostic::toText() const {
    std::ostringstream oss;
    oss << '[' << severityToString(severity) << "]: " << moduleName << " (" << modulePath << "), "
        << "line " << (range.start.line + 1) << ", char " << (range.start.character + 1) << ": \n"
        << message << "(name=" << name << ", code=0x" << hex8(diagCode()) << ")\n";

    if (!suggestion.empty())
        oss << "suggestion: " << suggestion;
    return oss.str();
}

std::string Diagnostic::toJson() const {
    std::ostringstream oss;
    oss << "{"
        << "\"range\":{\"start\":{\"line\":" << range.start.line
        << ",\"character\":" << range.start.character << "},"
        << "\"end\":{\"line\":" << range.end.line << ",\"character\":" << range.end.character << "}"
        << "},"
        << "\"severity\":" << severityToDiagnosticInt(severity) << ","
        << "\"code\":\"0x" << hex8(diagCode()) << "\","
        << "\"source\":\"Camel\","
        << "\"message\":\"" << escapeJson(message) << "\","
        << "\"data\":{"
        << "\"name\":\"" << escapeJson(name) << "\","
        << "\"moduleName\":\"" << escapeJson(moduleName) << "\","
        << "\"modulePath\":\"" << escapeJson(modulePath) << "\"";
    if (!suggestion.empty())
        oss << ",\"suggestion\":\"" << escapeJson(suggestion) << "\"";
    oss << "}"
        << "}";
    return oss.str();
}

uint32_t Diagnostic::diagCode() const { return makeDiagCode(type, specific); }

std::string Diagnostic::hex8(uint32_t v) {
    static const char *d = "0123456789ABCDEF";
    std::string out(8, '0');
    for (int i = 7; i >= 0; --i) {
        out[i] = d[v & 0xF];
        v >>= 4;
    }
    return out;
}

std::string Diagnostic::escapeJson(const std::string &s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
        case '\"':
            o << "\\\"";
            break;
        case '\\':
            o << "\\\\";
            break;
        case '\n':
            o << "\\n";
            break;
        case '\r':
            o << "\\r";
            break;
        case '\t':
            o << "\\t";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                o << "\\u00" << hexNib((c >> 4) & 0xF) << hexNib(c & 0xF);
            } else
                o << c;
        }
    }
    return o.str();
}

char Diagnostic::hexNib(int v) {
    static const char *d = "0123456789ABCDEF";
    return d[v & 0xF];
}

Diagnostic &Diagnostics::add(Diagnostic &&d) {
    std::lock_guard<std::mutex> lk(mtx_);
    // Check limits before adding
    checkLimits(d);

    storage_.push_back(std::move(d));
    return storage_.back();
}

void Diagnostics::setTokens(const std::vector<antlr4::Token *> *tokens) {
    std::lock_guard<std::mutex> lk(mtx_);
    rangeManager_.setTokens(tokens);
}

void Diagnostics::fetchAll() {
    std::lock_guard<std::mutex> lk(mtx_);
    for (auto &d : storage_)
        d.fetchRange(rangeManager_);
}

void Diagnostics::outputAll(std::ostream &os) const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto &d : storage_) {
        os << d.toText() << '\n';
    }
}

std::string Diagnostics::toJson() const {
    std::ostringstream oss;
    std::lock_guard<std::mutex> lk(mtx_);

    oss << "[";
    bool first = true;
    for (const auto &d : storage_) {
        if (!first)
            oss << ",";
        oss << d.toJson();
        first = false;
    }
    oss << "]";
    return oss.str();
}

void Diagnostics::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    storage_.clear();
}

// ---- Query implementations ----
std::vector<const Diagnostic *> Diagnostics::findByType(DiagType type) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<const Diagnostic *> result;

    for (const auto &d : storage_) {
        if (d.type == type) {
            result.push_back(&d);
        }
    }
    return result;
}

std::vector<const Diagnostic *> Diagnostics::findBySeverity(Severity severity) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<const Diagnostic *> result;

    for (const auto &d : storage_) {
        if (d.severity == severity) {
            result.push_back(&d);
        }
    }
    return result;
}

std::vector<const Diagnostic *>
Diagnostics::findByTypeAndSeverity(DiagType type, Severity severity) const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::vector<const Diagnostic *> result;

    for (const auto &d : storage_) {
        if (d.type == type && d.severity == severity) {
            result.push_back(&d);
        }
    }
    return result;
}

// ---- Statistics implementations ----
size_t Diagnostics::count() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return storage_.size();
}

size_t Diagnostics::count(Severity severity) const {
    std::lock_guard<std::mutex> lk(mtx_);
    return countBySeverityInternal(severity);
}

size_t Diagnostics::count(DiagType type) const {
    std::lock_guard<std::mutex> lk(mtx_);
    size_t result = 0;
    for (const auto &d : storage_) {
        if (d.type == type) {
            ++result;
        }
    }
    return result;
}

std::unordered_map<Severity, size_t> Diagnostics::countBySeverity() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::unordered_map<Severity, size_t> result;

    for (const auto &d : storage_) {
        result[d.severity]++;
    }
    return result;
}

std::unordered_map<DiagType, size_t> Diagnostics::countByType() const {
    std::lock_guard<std::mutex> lk(mtx_);
    std::unordered_map<DiagType, size_t> result;

    for (const auto &d : storage_) {
        result[d.type]++;
    }
    return result;
}

// ---- Helper implementations ----
void Diagnostics::checkLimits(const Diagnostic &d) {
    // Check total limit
    if (config_.hasTotalLimit() && static_cast<int>(storage_.size()) >= config_.total_limit) {
        throw DiagnosticsExceededTotalLimitException(config_.total_limit, d);
    }

    // Check per-severity limit
    if (config_.hasSeverityLimit(d.severity)) {
        size_t currentCount = countBySeverityInternal(d.severity);
        int limit = config_.getSeverityLimit(d.severity);
        if (static_cast<int>(currentCount) >= limit) {
            throw DiagnosticsExceededLimitException(d.severity, limit, d);
        }
    }
}

size_t Diagnostics::countBySeverityInternal(Severity severity) const {
    size_t result = 0;
    for (const auto &diag : storage_) {
        if (diag.severity == severity) {
            ++result;
        }
    }
    return result;
}

// ---- DiagnosticBuilder implementation ----
DiagnosticBuilder &DiagnosticBuilder::at(const TokenIndexRange &range) {
    tokenRange_ = range;
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::at(size_t start, size_t end) {
    tokenRange_ = {start, end};
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::at(size_t token) {
    tokenRange_ = {token, token};
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::modName(const std::string &name) {
    moduleName_ = name;
    return *this;
}

DiagnosticBuilder &DiagnosticBuilder::modPath(const std::string &path) {
    modulePath_ = path;
    return *this;
}