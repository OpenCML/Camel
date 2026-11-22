// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/error/diagnostics/base.h"
#include "camel/core/error/diagnostics/messages/internal.h"
#include "camel/core/error/diagnostics/messages/lexical.h"
#include "camel/core/error/diagnostics/messages/runtime.h"
#include "camel/core/error/diagnostics/messages/semantic.h"
#include "camel/core/error/diagnostics/messages/syntax.h"
#include "camel/utils/assert.h"

#include <algorithm>

namespace camel::core::error {

std::string to_string(DiagType type) {
    switch (type) {
    case DiagType::InternalDiag:
        return "InternalDiag";
    case DiagType::LexicalDiag:
        return "LexicalDiag";
    case DiagType::SyntaxDiag:
        return "SyntaxDiag";
    case DiagType::SemanticDiag:
        return "SemanticDiag";
    case DiagType::RuntimeDiag:
        return "RuntimeDiag";
    default:
        return "Unknown";
    }
}

std::string to_string(Severity s) {
    switch (s) {
    case Severity::Error:
        return "Error";
    case Severity::Warn:
        return "Warn";
    case Severity::Info:
        return "Info";
    case Severity::Hint:
        return "Hint";
    default:
        return "Unknown";
    }
}

std::string to_colorful_string(Severity s) {
    switch (s) {
    case Severity::Error:
        return "\033[1;31mError\033[0m";
    case Severity::Warn:
        return "\033[1;33mWarn\033[0m";
    case Severity::Info:
        return "\033[1;32mInfo\033[0m";
    case Severity::Hint:
        return "\033[1;36mHint\033[0m";
    default:
        return "\033[1;35mUnknown\033[0m";
    }
}

DiagInfo getDiagInfo(DiagType type, uint32_t specific) {
    switch (type) {
    case DiagType::InternalDiag: {
        const auto &map = getInternalDiagInfoMap();
        auto it         = map.find(static_cast<InternalDiag>(specific));
        if (it != map.end()) {
            return it->second;
        }
        break;
    }
    case DiagType::LexicalDiag: {
        const auto &map = getLexicalDiagInfoMap();
        auto it         = map.find(static_cast<LexicalDiag>(specific));
        if (it != map.end()) {
            return it->second;
        }
        break;
    }
    case DiagType::SyntaxDiag: {
        const auto &map = getSyntaxDiagInfoMap();
        auto it         = map.find(static_cast<SyntaxDiag>(specific));
        if (it != map.end()) {
            return it->second;
        }
        break;
    }
    case DiagType::SemanticDiag: {
        const auto &map = getSemanticDiagInfoMap();
        auto it         = map.find(static_cast<SemanticDiag>(specific));
        if (it != map.end()) {
            return it->second;
        }
        break;
    }
    case DiagType::RuntimeDiag: {
        const auto &map = getRuntimeDiagInfoMap();
        auto it         = map.find(static_cast<RuntimeDiag>(specific));
        if (it != map.end()) {
            return it->second;
        }
        break;
    }
    default:
        ASSERT(false, "Unsupported DiagType");
    }
    return DiagInfo{"Unknown", "Unknown diagnostic", ""};
}

const std::vector<DiagCatalogEntry> &diagnosticCatalog() {
    static const std::vector<DiagCatalogEntry> catalog = [] {
        std::vector<DiagCatalogEntry> entries;
        auto collect = [&](DiagType type, const auto &map) {
            for (const auto &[key, info] : map) {
                const uint32_t specific = static_cast<uint32_t>(key);
                entries.push_back(
                    DiagCatalogEntry{
                        type,
                        makeDiagCode(type, specific),
                        extractSeverity(specific),
                        info.name,
                        info.message,
                        info.suggestion});
            }
        };
        collect(DiagType::InternalDiag, getInternalDiagInfoMap());
        collect(DiagType::LexicalDiag, getLexicalDiagInfoMap());
        collect(DiagType::SyntaxDiag, getSyntaxDiagInfoMap());
        collect(DiagType::SemanticDiag, getSemanticDiagInfoMap());
        collect(DiagType::RuntimeDiag, getRuntimeDiagInfoMap());
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) {
            return a.code < b.code;
        });
        return entries;
    }();
    return catalog;
}

DiagInfo getDiagInfo(uint32_t diagCode) {
    DiagType type     = extractDiagType(diagCode);
    uint32_t specific = extractSpecific(diagCode);
    return getDiagInfo(type, specific);
}

} // namespace camel::core::error
