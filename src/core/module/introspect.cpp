// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

#include "camel/core/module/introspect.h"

#include "camel/core/operator_traits.h"
#include "camel/core/type/resolver.h"
#include "camel/utils/log.h"

#include <algorithm>
#include <format>
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

/// A JSON list of strings, from a ", "-separated list (permissionsText).
std::string jsonList(std::string_view commaSeparated) {
    std::string out = "[";
    size_t start    = 0;
    while (start < commaSeparated.size()) {
        size_t end = commaSeparated.find(", ", start);
        end        = end == std::string_view::npos ? commaSeparated.size() : end;
        out += (out.size() > 1 ? "," : "") + jsonString(commaSeparated.substr(start, end - start));
        start = end + 2;
    }
    return out + "]";
}

/// The "semantics" member of an OPERATOR line (introspect.h).
std::string semanticsJson(const sem::OperatorSemantics &s) {
    const sem::NumericSemantics &n = s.numeric;
    std::string out                = "{\"key\":" + jsonString(s.key.toString()) +
                      ",\"numeric\":" + jsonString(sem::numericClassName(n.cls));
    if (n.compute == type::TypeCode::Float32) {
        out += ",\"compute\":\"f32\"";
    } else if (n.compute == type::TypeCode::Float64) {
        out += ",\"compute\":\"f64\"";
    }
    if (n.cls == sem::NumericClass::Bounded) {
        out += std::format(",\"ulp\":{}", n.ulpBound);
    }
    out += ",\"sensitive_to\":" + jsonList(sem::permissionsText(n.sensitiveTo));
    std::string departures;
    for (const auto &[ieee, name] : {std::pair{n.specials.nans, "nans"},
                                     std::pair{n.specials.infinities, "infinities"},
                                     std::pair{n.specials.signedZeros, "signed_zeros"},
                                     std::pair{n.specials.subnormals, "subnormals"}}) {
        if (!ieee) {
            departures += (departures.empty() ? "" : ", ") + std::string(name);
        }
    }
    if (!departures.empty()) {
        out += ",\"not_ieee\":" + jsonList(departures);
    }
    if (s.faults) {
        std::string faults;
        for (const sem::FaultSpec &f : *s.faults) {
            faults += (faults.empty() ? "" : ", ") + std::string(sem::faultKindName(f.kind));
        }
        out += ",\"faults\":" + jsonList(faults);
    }
    if (s.indexing) {
        out += ",\"indexing\":true";
    }
    if (!s.laws.empty()) {
        std::string laws;
        for (const sem::ReductionLaw &law : s.laws) {
            laws += (laws.empty() ? "" : ", ") + law.name;
        }
        out += ",\"laws\":" + jsonList(laws);
    }
    if (s.resources) {
        out += ",\"resources\":" + jsonString(sem::toString(*s.resources));
    }
    if (s.definition) {
        // "library::function(norm 0, norm 1 = -1)": the function and how operands bind it.
        std::string params;
        for (const sem::DefinitionParam &p : s.definition->params) {
            params += std::format(
                "{}{} {}",
                params.empty() ? "" : ", ",
                p.kind == sem::DefinitionParam::Kind::Norm ? "norm" : "with",
                p.operand);
            if (p.fallback) {
                params += " = " + std::visit(
                                      [](const auto &v) {
                                          if constexpr (std::is_same_v<std::decay_t<decltype(v)>, bool>) {
                                              return std::string(v ? "true" : "false");
                                          } else {
                                              return std::format("{}", v);
                                          }
                                      },
                                      *p.fallback);
            }
        }
        out += ",\"definition\":" +
               jsonString(std::format("{}::{}({})", s.definition->module, s.definition->function, params));
    }
    return out + ",\"algorithm\":" + jsonString(n.algorithm) + "}";
}

} // namespace

std::vector<OperatorDescription> describeOperators(const Module &module) {
    std::vector<OperatorDescription> result;
    const auto entities = module.nativeEntityNS();
    if (!entities) {
        return result;
    }
    const auto &traits = OperatorTable::instance();
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
            } else if (
                auto *declared =
                    dynamic_cast<type::DeclaredFuncTypeResolver *>(resolver.get())) {
                // A body-less declaration: the written signature says the parameter counts.
                d.withCount = static_cast<int>(declared->withCount());
                d.normCount = static_cast<int>(declared->normCount());
            }
            if (const auto id = traits.lookup(uri)) {
                d.semantics = traits.semantics(*id);
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
               << ",\"signature\":" << jsonString(d.signature);
            if (d.semantics) {
                os << ",\"semantics\":" << semanticsJson(*d.semantics);
            }
            os << "}\n";
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
        // which costs optimizations and never correctness (design principles 3.3).
        CAMEL_LOG_WARN_S(
            "Module",
            "Module '{}' does not declare whether {} of its operators are pure ({}{}); they are "
            "treated as having side effects. Declare each with OperatorTable::declare.",
            module.name(),
            count,
            names,
            count > 8 ? ", ..." : "");
    }
    return count;
}

} // namespace camel::core::module
