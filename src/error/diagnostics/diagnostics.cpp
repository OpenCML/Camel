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
 * Updated: Oct. 12, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "diagnostics.h"

#include "utils/ascii.h"
#include "utils/assert.h"
#include "utils/log.h"
#include "utils/str.h"

#include <format>

// ---- Diagnostic implementation ----
Diagnostic &Diagnostic::fetchRange(const RangeConverter &conv) {
    if (std::holds_alternative<TokenRange>(range)) {
        TokenRange tr = std::get<TokenRange>(range);
        range = conv.conv(tr);
    }
    return *this;
}

std::string Diagnostic::toText() const {
    int ln = -1, ch = -1;

    if (std::holds_alternative<TokenRange>(range)) {
        l.in("Diag").warn("TokenRange should be converted to CharRange before toText()");
    } else if (std::holds_alternative<CharRange>(range)) {
        CharRange r = std::get<CharRange>(range);
        ln = static_cast<int>(r.start.line + 1);
        ch = static_cast<int>(r.start.character + 1);
    }

    std::string result = std::format(
        "{}({}):{}:{}: [{}]: {} {} (name={}, code=0x{})",
        ascii::underline(modulePath),
        moduleName,
        (ln >= 0 ? std::to_string(ln) : "?"),
        (ch >= 0 ? std::to_string(ch) : "?"),
        to_colorful_string(severity),
        message,
        suggestion,
        name,
        hex8(diagCode()));

    return result;
}

std::string Diagnostic::toJson() const {
    std::ostringstream oss;
    CharRange r{{0, 0}, {0, 0}};
    if (std::holds_alternative<CharRange>(range)) {
        r = std::get<CharRange>(range);
    } else if (std::holds_alternative<TokenRange>(range)) {
        l.in("Diag").warn("TokenRange should be converted to CharRange before toJson()");
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
    d.moduleName = moduleName_;
    d.modulePath = modulePath_;
    std::lock_guard<std::mutex> lk(mtx_);
    // Check limits before adding
    checkLimits(d);
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

void Diagnostics::dump(std::ostream &os, bool json) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (json)
        os << "[\n";
    os << strutil::join(storage_, (json ? ",\n" : "\n"), [json](const Diagnostic &d) {
        return json ? d.toJson() : d.toText();
    });
    if (json)
        os << "\n]";
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
        int limit = config_.getSeverityLimit(d.severity);
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
