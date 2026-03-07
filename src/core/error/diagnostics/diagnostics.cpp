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
 * Updated: Mar. 07, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/error/diagnostics.h"

#include "camel/utils/ascii.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"
#include "camel/utils/str.h"

#include <filesystem>
#include <format>

namespace {

std::string effectiveModulePath(const Diagnostic &d) {
    if (!d.modulePath.empty()) {
        return d.modulePath;
    }
    if (std::holds_alternative<camel::source::span_id_t>(d.range) && d.sourceContext) {
        return d.sourceContext->pathForSpan(std::get<camel::source::span_id_t>(d.range));
    }
    if (std::holds_alternative<camel::source::origin_id_t>(d.range) && d.sourceContext) {
        return d.sourceContext->pathForOrigin(std::get<camel::source::origin_id_t>(d.range));
    }
    return "";
}

std::string effectiveModuleName(const Diagnostic &d, const std::string &path) {
    if (!d.moduleName.empty()) {
        return d.moduleName;
    }
    if (path.empty()) {
        return "";
    }
    return std::filesystem::path(path).stem().string();
}

} // namespace

// ---- Diagnostic implementation ----
Diagnostic &Diagnostic::fetchRange(const RangeConverter &conv) {
    if (std::holds_alternative<TokenRange>(range)) {
        TokenRange tr = std::get<TokenRange>(range);
        range         = conv.conv(tr);
    } else if (std::holds_alternative<camel::source::span_id_t>(range) && sourceContext) {
        range = sourceContext->resolveSpan(std::get<camel::source::span_id_t>(range));
    } else if (std::holds_alternative<camel::source::origin_id_t>(range) && sourceContext) {
        range = sourceContext->resolveOrigin(std::get<camel::source::origin_id_t>(range));
    }
    return *this;
}

std::string Diagnostic::toText() const {
    int ln = -1, ch = -1;
    std::string path       = effectiveModulePath(*this);
    std::string moduleName = effectiveModuleName(*this, path);
    std::string diagName   = name;

    if (std::holds_alternative<TokenRange>(range)) {
        GetDefaultLogger().in("Diag").warn(
            "TokenRange should be converted to CharRange before toText()");
    } else if (std::holds_alternative<camel::source::span_id_t>(range) && sourceContext) {
        CharRange r = sourceContext->resolveSpan(std::get<camel::source::span_id_t>(range));
        ln          = static_cast<int>(r.start.line + 1);
        ch          = static_cast<int>(r.start.character + 1);
    } else if (std::holds_alternative<camel::source::origin_id_t>(range) && sourceContext) {
        CharRange r = sourceContext->resolveOrigin(std::get<camel::source::origin_id_t>(range));
        ln          = static_cast<int>(r.start.line + 1);
        ch          = static_cast<int>(r.start.character + 1);
    } else if (std::holds_alternative<CharRange>(range)) {
        CharRange r = std::get<CharRange>(range);
        ln          = static_cast<int>(r.start.line + 1);
        ch          = static_cast<int>(r.start.character + 1);
    }

    std::string result = std::format(
        "{}({}):{}:{}: [{}]: {} {} (name={}, code=0x{})",
        ascii::underline(path),
        moduleName,
        (ln >= 0 ? std::to_string(ln) : "?"),
        (ch >= 0 ? std::to_string(ch) : "?"),
        to_colorful_string(severity),
        message,
        suggestion,
        diagName,
        hex8(diagCode()));

    return result;
}

std::string Diagnostic::toJson() const {
    std::ostringstream oss;
    CharRange r{{0, 0}, {0, 0}};
    std::string path       = effectiveModulePath(*this);
    std::string moduleName = effectiveModuleName(*this, path);
    std::string diagName   = name;
    if (std::holds_alternative<CharRange>(range)) {
        r = std::get<CharRange>(range);
    } else if (std::holds_alternative<camel::source::span_id_t>(range) && sourceContext) {
        r = sourceContext->resolveSpan(std::get<camel::source::span_id_t>(range));
    } else if (std::holds_alternative<camel::source::origin_id_t>(range) && sourceContext) {
        r = sourceContext->resolveOrigin(std::get<camel::source::origin_id_t>(range));
    } else if (std::holds_alternative<TokenRange>(range)) {
        GetDefaultLogger().in("Diag").warn(
            "TokenRange should be converted to CharRange before toJson()");
    }
    oss << "{"
        << "\"range\":{\"start\":{\"line\":" << r.start.line
        << ",\"character\":" << r.start.character << "},"
        << "\"end\":{\"line\":" << r.end.line << ",\"character\":" << r.end.character << "}"
        << "},"
        << "\"severity\":" << to_string(severity) << ","
        << "\"code\":\"0x" << hex8(diagCode()) << "\","
        << "\"source\":\"Camel\","
        << "\"message\":\"" << escapeJson(message) << "\","
        << "\"data\":{"
        << "\"name\":\"" << escapeJson(diagName) << "\","
        << "\"moduleName\":\"" << escapeJson(moduleName) << "\","
        << "\"modulePath\":\"" << escapeJson(path) << "\"";
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
    if (d.moduleName.empty()) {
        d.moduleName = moduleName_;
    }
    if (d.modulePath.empty()) {
        d.modulePath = modulePath_;
    }
    if (!d.sourceContext) {
        d.sourceContext = sourceContext_;
    }
    d.persisted = true;
    std::lock_guard<std::mutex> lk(mtx_);
    // Check limits before adding
    storage_.push_back(std::move(d));
    checkLimits(d);
    return storage_.back();
}

void Diagnostics::fetchAll(const std::vector<antlr4::Token *> &tokens) {
    std::lock_guard<std::mutex> lk(mtx_);
    RangeConverter conv(tokens);
    for (auto &d : storage_)
        d.fetchRange(conv);
}

void Diagnostics::dump(std::ostream &os, bool json, bool wrapInArray) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (json && wrapInArray)
        os << "[\n";
    os << strutil::join(storage_, (json ? ",\n" : "\n"), [json](const Diagnostic &d) {
        return json ? d.toJson() : d.toText();
    });
    if (json && wrapInArray)
        os << "\n]";
    if (wrapInArray)
        os << "\n" << std::flush;
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

bool Diagnostics::hasErrors() const {
    std::lock_guard<std::mutex> lk(mtx_);
    for (const auto &d : storage_) {
        if (d.severity == Severity::Error) {
            return true;
        }
    }
    return false;
}

// ---- Helper implementations ----
void Diagnostics::checkLimits(const Diagnostic &d) {
    // Check total limit
    if (config_.hasTotalLimit() && static_cast<int>(storage_.size()) >= config_.total_limit) {
        throw DiagnosticsTotalLimitExceededException(config_.total_limit);
    }

    // Check per-severity limit
    if (config_.hasSeverityLimit(d.severity)) {
        size_t currentCount = countBySeverityInternal(d.severity) + 1;
        int limit           = config_.getSeverityLimit(d.severity);
        if (static_cast<int>(currentCount) >= limit) {
            throw DiagnosticsLimitExceededException(d.severity, limit);
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
