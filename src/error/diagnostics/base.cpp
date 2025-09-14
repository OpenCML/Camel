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
 * Created: Sep. 11, 2025
 * Updated: Sep. 11, 2025
 * Supported by: National Key Research and Development Program of China
 */

#include "base.h"
#include "utils/assert.h"

#include "messages/internal.h"
#include "messages/lexical.h"
#include "messages/runtime.h"
#include "messages/semantic.h"
#include "messages/syntax.h"

std::string to_string(Severity s) {
    switch (s) {
    case Severity::Error:
        return "Error";
    case Severity::Warning:
        return "Warning";
    case Severity::Info:
        return "Info";
    case Severity::Hint:
        return "Hint";
    default:
        return "Unknown";
    }
}

DiagInfo getDiagInfo(DiagType type, uint32_t specific) {
    switch (type) {
    case DiagType::InternalDiag: {
        const auto &map = getInternalDiagInfoMap();
        auto it = map.find(static_cast<InternalDiag>(specific));
        if (it != map.end()) {
            return it->second;
        }
        break;
    }
    case DiagType::LexicalDiag: {
        const auto &map = getLexicalDiagInfoMap();
        auto it = map.find(static_cast<LexicalDiag>(specific));
        if (it != map.end()) {
            return it->second;
        }
        break;
    }
    case DiagType::SyntaxDiag: {
        const auto &map = getSyntaxDiagInfoMap();
        auto it = map.find(static_cast<SyntaxDiag>(specific));
        if (it != map.end()) {
            return it->second;
        }
        break;
    }
    case DiagType::SemanticDiag: {
        const auto &map = getSemanticDiagInfoMap();
        auto it = map.find(static_cast<SemanticDiag>(specific));
        if (it != map.end()) {
            return it->second;
        }
        break;
    }
    case DiagType::RuntimeDiag: {
        const auto &map = getRuntimeDiagInfoMap();
        auto it = map.find(static_cast<RuntimeDiag>(specific));
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

DiagInfo getDiagInfo(uint32_t diagCode) {
    DiagType type = extractDiagType(diagCode);
    uint32_t specific = extractSpecific(diagCode);
    return getDiagInfo(type, specific);
}