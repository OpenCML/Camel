// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/error/diagnostics.h"

#include "camel/utils/ascii.h"
#include "camel/utils/assert.h"
#include "camel/utils/log.h"
#include "camel/utils/path.h"
#include "camel/utils/str.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <format>
#include <iostream>
#include <sstream>

namespace camel::core::error {

std::string diagnosticPath(const Diagnostic &d) {
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

std::string diagnosticModuleName(const Diagnostic &d, const std::string &path) {
    if (!d.moduleName.empty()) {
        return d.moduleName;
    }
    if (path.empty()) {
        return "";
    }
    return camel::utils::pathToUtf8(camel::utils::pathFromUtf8(path).stem());
}

std::optional<CharRange>
resolveSourceRange(const SourceRange &range, const camel::source::source_context_ptr_t &ctx) {
    if (std::holds_alternative<CharRange>(range)) {
        return std::get<CharRange>(range);
    }
    if (std::holds_alternative<camel::source::span_id_t>(range) && ctx) {
        return ctx->resolveSpan(std::get<camel::source::span_id_t>(range));
    }
    if (std::holds_alternative<camel::source::origin_id_t>(range) && ctx) {
        return ctx->resolveOrigin(std::get<camel::source::origin_id_t>(range));
    }
    if (std::holds_alternative<TokenRange>(range)) {
        CAMEL_LOG_WARN_S("Diag", "TokenRange should be converted to CharRange before output");
    }
    return std::nullopt;
}

// ---- Warning policy ----
namespace {
std::mutex &warningPolicyMutex() {
    static std::mutex m;
    return m;
}
WarningPolicy &warningPolicySlot() {
    static WarningPolicy policy;
    return policy;
}
bool listsName(const std::vector<std::string> &names, const std::string &key) {
    return std::find(names.begin(), names.end(), key) != names.end();
}
} // namespace

std::string normalizeDiagnosticName(std::string_view name) {
    if (auto at = name.rfind("::"); at != std::string_view::npos) {
        name.remove_prefix(at + 2);
    }
    std::string key;
    for (char c : name) {
        if (c == '-' || c == '_') {
            continue;
        }
        key += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return key;
}

void setWarningPolicy(WarningPolicy policy) {
    for (auto *names : {&policy.disabled, &policy.asErrors, &policy.notAsErrors}) {
        for (std::string &name : *names) {
            name = normalizeDiagnosticName(name);
        }
    }
    std::lock_guard<std::mutex> lk(warningPolicyMutex());
    warningPolicySlot() = std::move(policy);
}

namespace {
std::atomic<size_t> g_warningsShown{0};
std::atomic<size_t> g_warningsNotShown{0};
} // namespace

bool admitShownWarning() {
    size_t limit;
    {
        std::lock_guard<std::mutex> lk(warningPolicyMutex());
        limit = warningPolicySlot().maxShown;
    }
    if (g_warningsShown.fetch_add(1) < limit) {
        return true;
    }
    g_warningsNotShown.fetch_add(1);
    return false;
}

size_t warningsNotShown() { return g_warningsNotShown.load(); }

bool applyWarningPolicy(Diagnostic &d) {
    if (d.severity != Severity::Warn || d.promoted) {
        return !d.suppressed;
    }
    const std::string key = normalizeDiagnosticName(d.name);
    std::lock_guard<std::mutex> lk(warningPolicyMutex());
    const WarningPolicy &policy = warningPolicySlot();
    // A warning named in -Werror=<name> is wanted as an error, which outranks a blanket
    // switch-off; one switched off by name is gone whatever -Werror says.
    const bool errorByName = listsName(policy.asErrors, key);
    if (listsName(policy.disabled, key) || (policy.disableAll && !errorByName)) {
        d.suppressed = true;
        return false;
    }
    if (errorByName || (policy.allAsErrors && !listsName(policy.notAsErrors, key))) {
        d.severity = Severity::Error;
        d.promoted = true;
    }
    return true;
}

// ---- Diagnostic implementation ----
Diagnostic &Diagnostic::fetchRange(const RangeConverter &conv) {
    auto resolve = [&](SourceRange &r) {
        if (std::holds_alternative<TokenRange>(r)) {
            TokenRange tr = std::get<TokenRange>(r);
            r             = conv.conv(tr);
        } else if (std::holds_alternative<camel::source::span_id_t>(r) && sourceContext) {
            r = sourceContext->resolveSpan(std::get<camel::source::span_id_t>(r));
        } else if (std::holds_alternative<camel::source::origin_id_t>(r) && sourceContext) {
            r = sourceContext->resolveOrigin(std::get<camel::source::origin_id_t>(r));
        }
    };
    // A secondary location keeps the file of its span / origin once that is turned into a
    // plain range.
    for (DiagLabel &label : related) {
        if (label.path.empty() && sourceContext) {
            if (std::holds_alternative<camel::source::span_id_t>(label.range)) {
                label.path =
                    sourceContext->pathForSpan(std::get<camel::source::span_id_t>(label.range));
            } else if (std::holds_alternative<camel::source::origin_id_t>(label.range)) {
                label.path = sourceContext->pathForOrigin(
                    std::get<camel::source::origin_id_t>(label.range));
            }
        }
        resolve(label.range);
    }
    if (modulePath.empty()) {
        modulePath = diagnosticPath(*this);
    }
    resolve(range);
    return *this;
}

namespace {
std::mutex &pipelineStageMutex() {
    static std::mutex m;
    return m;
}
std::string &pipelineStageSlot() {
    static std::string stage;
    return stage;
}
} // namespace

void setPipelineStage(std::string stage) {
    std::lock_guard<std::mutex> lk(pipelineStageMutex());
    pipelineStageSlot() = std::move(stage);
}

std::string stageForDiagnostic(DiagType type) {
    switch (type) {
    case DiagType::LexicalDiag:
    case DiagType::SyntaxDiag:
        return "parse";
    case DiagType::SemanticDiag:
        return "typecheck";
    default:
        break;
    }
    {
        std::lock_guard<std::mutex> lk(pipelineStageMutex());
        if (!pipelineStageSlot().empty()) {
            return pipelineStageSlot();
        }
    }
    return type == DiagType::InternalDiag ? "internal" : "run";
}

std::string Diagnostic::toText() const { return renderDiagnostic(*this, defaultRenderOptions()); }

std::string Diagnostic::toText(const std::ostream &os) const {
    return renderDiagnostic(*this, renderOptionsFor(os));
}

std::string Diagnostic::toJson() const {
    std::ostringstream oss;
    std::string path       = diagnosticPath(*this);
    std::string moduleName = diagnosticModuleName(*this, path);
    std::string diagName   = name;
    auto rangeJson         = [](const CharRange &r) {
        return std::format(
            "{{\"start\":{{\"line\":{},\"character\":{}}},\"end\":{{\"line\":{},\"character\":{}}}}}",
            r.start.line,
            r.start.character,
            r.end.line,
            r.end.character);
    };
    const CharRange r = resolveSourceRange(range, sourceContext).value_or(CharRange{{0, 0}, {0, 0}});
    oss << "{"
        << "\"range\":" << rangeJson(r) << ","
        << "\"severity\":\"" << to_string(severity) << "\","
        << "\"code\":\"0x" << hex8(diagCode()) << "\","
        << "\"source\":\"Camel\","
        << "\"message\":\"" << escapeJson(message) << "\",";
    // Secondary locations, in the shape of LSP's relatedInformation with a path for the URI.
    if (!related.empty()) {
        oss << "\"relatedInformation\":[";
        for (size_t i = 0; i < related.size(); ++i) {
            const DiagLabel &label = related[i];
            std::string labelPath  = label.path;
            if (labelPath.empty() && sourceContext) {
                if (std::holds_alternative<camel::source::span_id_t>(label.range)) {
                    labelPath =
                        sourceContext->pathForSpan(std::get<camel::source::span_id_t>(label.range));
                } else if (std::holds_alternative<camel::source::origin_id_t>(label.range)) {
                    labelPath = sourceContext->pathForOrigin(
                        std::get<camel::source::origin_id_t>(label.range));
                }
            }
            if (labelPath.empty()) {
                labelPath = path;
            }
            oss << (i ? "," : "") << "{\"location\":{\"path\":\"" << escapeJson(labelPath)
                << "\",\"range\":"
                << rangeJson(
                       resolveSourceRange(label.range, sourceContext)
                           .value_or(CharRange{{0, 0}, {0, 0}}))
                << "},\"message\":\"" << escapeJson(label.message) << "\"}";
        }
        oss << "],";
    }
    oss << "\"data\":{"
        << "\"stage\":\"" << escapeJson(stage.empty() ? stageForDiagnostic(type) : stage) << "\","
        << "\"name\":\"" << escapeJson(diagName) << "\","
        << "\"moduleName\":\"" << escapeJson(moduleName) << "\","
        << "\"modulePath\":\"" << escapeJson(path) << "\"";
    if (!suggestion.empty())
        oss << ",\"suggestion\":\"" << escapeJson(suggestion) << "\"";
    // The changes a tool can apply, in the shape a code action carries: a title, and the edits
    // that carry it out. Only the reports that know the text to write carry any, so a reader that
    // sees `fixes` may apply them; a reader that does not must fall back to `suggestion`.
    if (!fixes.empty()) {
        oss << ",\"fixes\":[";
        for (size_t i = 0; i < fixes.size(); ++i) {
            oss << (i ? "," : "") << "{\"title\":\"" << escapeJson(fixes[i].title) << "\",\"edits\":[";
            for (size_t j = 0; j < fixes[i].edits.size(); ++j) {
                const DiagEdit &edit = fixes[i].edits[j];
                oss << (j ? "," : "") << "{\"range\":"
                    << rangeJson(
                           resolveSourceRange(edit.range, sourceContext)
                               .value_or(CharRange{{0, 0}, {0, 0}}))
                    << ",\"replacement\":\"" << escapeJson(edit.replacement) << "\"}";
            }
            oss << "]}";
        }
        oss << "]";
    }
    if (!notes.empty()) {
        oss << ",\"notes\":[";
        for (size_t i = 0; i < notes.size(); ++i) {
            oss << (i ? "," : "") << "\"" << escapeJson(notes[i]) << "\"";
        }
        oss << "]";
    }
    if (relatedElided)
        oss << ",\"relatedElided\":true";
    if (promoted)
        oss << ",\"promotedFromWarning\":true";
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
    if (!applyWarningPolicy(d)) {
        // Switched off on the command line: handed back marked `suppressed`, never stored.
        std::lock_guard<std::mutex> lk(mtx_);
        discarded_ = std::move(d);
        return discarded_;
    }
    if (d.moduleName.empty()) {
        d.moduleName = moduleName_;
    }
    if (d.modulePath.empty()) {
        d.modulePath = modulePath_;
    }
    if (!d.sourceContext) {
        d.sourceContext = sourceContext_;
    }
    if (d.stage.empty()) {
        d.stage = stageForDiagnostic(d.type);
    }
    d.persisted = true;
    std::lock_guard<std::mutex> lk(mtx_);
    // The same error at the same place with the same text is one error. It is found twice
    // when one piece of source is checked more than once: the body of a generic function is
    // checked per instance, and what does not depend on the type variables fails in each.
    if (d.severity == Severity::Error) {
        auto sameRange = [](const SourceRange &a, const SourceRange &b) {
            if (a.index() != b.index()) {
                return false;
            }
            if (std::holds_alternative<CharRange>(a)) {
                const CharRange &x = std::get<CharRange>(a), &y = std::get<CharRange>(b);
                return x.start.line == y.start.line && x.start.character == y.start.character &&
                       x.end.line == y.end.line && x.end.character == y.end.character;
            }
            if (std::holds_alternative<TokenRange>(a)) {
                return std::get<TokenRange>(a).start == std::get<TokenRange>(b).start &&
                       std::get<TokenRange>(a).end == std::get<TokenRange>(b).end;
            }
            if (std::holds_alternative<camel::source::span_id_t>(a)) {
                return std::get<camel::source::span_id_t>(a) ==
                       std::get<camel::source::span_id_t>(b);
            }
            if (std::holds_alternative<camel::source::origin_id_t>(a)) {
                return std::get<camel::source::origin_id_t>(a) ==
                       std::get<camel::source::origin_id_t>(b);
            }
            return false; // no position: nothing says the two are one
        };
        for (Diagnostic &known : storage_) {
            if (known.severity == Severity::Error && known.type == d.type &&
                known.specific == d.specific && known.message == d.message &&
                sameRange(known.range, d.range)) {
                return known;
            }
        }
    }
    storage_.push_back(std::move(d));
    // Limits apply to the element now at the back; do not read the moved-from `d` (UB).
    checkLimits();
    return storage_.back();
}

void Diagnostics::fetchAll(const TokenPositions &tokens) {
    std::lock_guard<std::mutex> lk(mtx_);
    RangeConverter conv(tokens);
    for (auto &d : storage_)
        d.fetchRange(conv);
}

void Diagnostics::dump(std::ostream &os, bool json, bool wrapInArray) const {
    std::lock_guard<std::mutex> lk(mtx_);
    if (json && wrapInArray)
        os << "[\n";
    // The form with source excerpts takes several lines; a blank line keeps them apart.
    const RenderOptions options = renderOptionsFor(os);
    const char *textSeparator   = options.format == DiagFormat::Rich ? "\n\n" : "\n";
    os << strutil::join(storage_, (json ? ",\n" : textSeparator), [&](const Diagnostic &d) {
        return json ? d.toJson() : renderDiagnostic(d, options);
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

void Diagnostics::addRelatedToErrorsFrom(size_t from, const DiagLabel &label, size_t limit) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (size_t i = from; i < storage_.size(); ++i) {
        Diagnostic &d = storage_[i];
        if (d.severity != Severity::Error) {
            continue;
        }
        if (limit > 0 && d.related.size() >= limit) {
            d.relatedElided = true;
            continue;
        }
        d.related.push_back(label);
    }
}

void Diagnostics::locateByOriginFrom(size_t from) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (size_t i = from; i < storage_.size(); ++i) {
        Diagnostic &d = storage_[i];
        if (!d.sourceContext || !std::holds_alternative<camel::source::origin_id_t>(d.range)) {
            continue;
        }
        const std::string path =
            d.sourceContext->pathForOrigin(std::get<camel::source::origin_id_t>(d.range));
        if (path.empty()) {
            continue;
        }
        std::error_code ec;
        const auto here =
            std::filesystem::weakly_canonical(camel::utils::pathFromUtf8(d.modulePath), ec);
        const auto there = std::filesystem::weakly_canonical(camel::utils::pathFromUtf8(path), ec);
        if (here != there) {
            d.modulePath = path;
        }
    }
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
void Diagnostics::checkLimits() {
    if (storage_.empty()) {
        return;
    }
    const Diagnostic &ref = storage_.back();

    // Total cap: after push, size must not exceed total_limit (when configured).
    if (config_.hasTotalLimit() && static_cast<int>(storage_.size()) > config_.total_limit) {
        throw DiagnosticsTotalLimitExceededException(config_.total_limit);
    }

    // Per-severity cap: count already includes the diagnostic just pushed (no +1).
    // limit == 0 means "allow zero stored diagnostics of this severity" -> throw on first.
    if (config_.hasSeverityLimit(ref.severity)) {
        const int limit = config_.getSeverityLimit(ref.severity);
        const size_t n  = countBySeverityInternal(ref.severity);
        if (static_cast<int>(n) > limit) {
            throw DiagnosticsLimitExceededException(ref.severity, static_cast<size_t>(limit));
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

} // namespace camel::core::error
