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
 * Author: Camel Contributors
 * Created: Oct. 12, 2026
 * Updated: Oct. 12, 2026
 * Supported by: National Key Research and Development Program of China
 */

#include "camel/core/module/introspect.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <ostream>

namespace camel::core::module {

namespace {

std::string jsonString(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    out += '"';
    return out;
}

} // namespace

std::vector<OperatorDescription> describeOperators(const Module &module) {
    std::vector<OperatorDescription> result;
    const auto entities = module.exportedEntityNS();
    if (!entities) {
        return result;
    }
    const auto &traits = OperatorTraitsRegistry::instance();
    entities->forEach([&](const Reference &ref, const entity &ent) {
        if (!ent.isOperGroup() || !ent.operGroup()) {
            return;
        }
        for (const auto &[uri, resolver] : ent.operGroup()->resolvers()) {
            OperatorDescription d;
            d.module    = module.name();
            d.name      = ref.toString();
            d.uri       = uri;
            d.signature = resolver ? resolver->signature() : "";
            if (auto *fixed = dynamic_cast<type::StaticFuncTypeResolver *>(resolver.get())) {
                d.withCount = static_cast<int>(fixed->funcType()->withTypesCount());
                d.normCount = static_cast<int>(fixed->funcType()->normTypesCount());
            } else if (
                auto *dynamic = dynamic_cast<type::DynamicFuncTypeResolver *>(resolver.get())) {
                d.withCount = dynamic->withCount();
                d.normCount = dynamic->normCount();
            }
            if (const auto declared = traits.find(uri)) {
                d.purity = declared->pure        ? OperatorPurity::Pure
                           : declared->stateOnly ? OperatorPurity::StateOnly
                                                 : OperatorPurity::Effectful;
            }
            result.push_back(std::move(d));
        }
    });
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return std::tie(a.name, a.uri) < std::tie(b.name, b.uri);
    });
    return result;
}

std::string_view to_string(OperatorPurity purity) {
    switch (purity) {
    case OperatorPurity::Pure:
        return "pure";
    case OperatorPurity::Effectful:
        return "effectful";
    case OperatorPurity::StateOnly:
        return "state";
    default:
        return "undeclared";
    }
}

void dumpOperators(const std::vector<module_ptr_t> &modules, std::ostream &os) {
    for (const auto &module : modules) {
        if (!module) {
            continue;
        }
        for (const OperatorDescription &d : describeOperators(*module)) {
            os << "OPERATOR {\"module\":" << jsonString(d.module.empty() ? "<builtin>" : d.module)
               << ",\"name\":" << jsonString(d.name) << ",\"uri\":" << jsonString(d.uri)
               << ",\"with\":" << d.withCount << ",\"norm\":" << d.normCount
               << ",\"purity\":" << jsonString(to_string(d.purity))
               << ",\"signature\":" << jsonString(d.signature) << "}\n";
        }
    }
}

size_t warnUndeclaredOperatorTraits(const Module &module) {
    size_t count = 0;
    std::string names;
    for (const OperatorDescription &d : describeOperators(module)) {
        if (d.purity != OperatorPurity::Undeclared) {
            continue;
        }
        ++count;
        if (count <= 8) {
            names += (names.empty() ? "" : ", ") + d.uri;
        }
    }
    if (count > 0) {
        // Not an error: an operator without a declaration is treated as having side effects,
        // which costs optimizations and never correctness (design principles 2.9).
        CAMEL_LOG_WARN_S(
            "Module",
            "Module '{}' does not declare whether {} of its operators are pure ({}{}); they are "
            "treated as having side effects. Declare each with OperatorTraitsRegistry::declare.",
            module.name(),
            count,
            names,
            count > 8 ? ", ..." : "");
    }
    return count;
}

} // namespace camel::core::module
