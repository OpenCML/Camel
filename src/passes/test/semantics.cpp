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
 * Created: Oct. 15, 2026
 * Updated: Oct. 15, 2026
 * Supported by: National Key Research and Development Program of China
 */

/*
 * TEST-ONLY checks of the operator semantics interface (see semantics.h).
 */

#include "semantics.h"

#include "camel/core/operator_table.h"

#include <ostream>
#include <stdexcept>

namespace {

using camel::core::OperatorTable;
using namespace camel::core::sem;
using camel::core::type::TypeCode;

/// A well-formed correctly rounded declaration of `uri` at the given version.
OperatorSemantics valid(std::string_view uri, uint16_t major = 1, uint16_t minor = 0) {
    return OperatorSemantics{
        .key = OperatorKey::of(uri, major, minor),
        .numeric =
            NumericSemantics{
                .cls         = NumericClass::CorrectlyRounded,
                .compute     = TypeCode::Float32,
                .sensitiveTo = kReassociate | kFiniteMath,
                .algorithm   = "a test operator",
            },
    };
}

/// Declares `semantics` for `uri` and prints whether the table accepted it.
void attempt(
    std::ostream &os, std::string_view check, std::string_view uri, OperatorSemantics semantics) {
    try {
        OperatorTable::instance().declareSemantics(uri, std::move(semantics));
        os << "SEMANTICS " << check << " accepted\n";
    } catch (const std::invalid_argument &e) {
        os << "SEMANTICS " << check << " rejected: " << e.what() << "\n";
    }
}

} // namespace

const camel::runtime::Program *
OperatorSemanticsTestPass::apply(const camel::runtime::Program *program, std::ostream &os) {
    OperatorTable &table = OperatorTable::instance();

    // ---- Registration checks (design 4.4): each declaration below breaks one rule. ----
    attempt(os, "valid", "semtest:ok", valid("semtest:ok", 1, 2));
    attempt(os, "key_names_other_operator", "semtest:a", valid("semtest:b"));
    attempt(os, "lower_version", "semtest:ok", valid("semtest:ok", 1, 1));
    table.set("semtest:nofault", {.pure = true, .mayFault = false});
    {
        OperatorSemantics s = valid("semtest:nofault");
        s.faults            = std::vector<FaultSpec>{{FaultKind::DivisionByZero, 1}};
        attempt(os, "faults_against_traits", "semtest:nofault", s);
    }
    {
        OperatorSemantics s     = valid("semtest:bounded");
        s.numeric.cls           = NumericClass::Bounded;
        s.numeric.sensitiveTo   = kApproxMath;
        attempt(os, "bounded_without_ulp", "semtest:bounded", s);
        s.numeric.ulpBound    = 2;
        s.numeric.sensitiveTo = kFiniteMath;
        attempt(os, "bounded_not_approx", "semtest:bounded", s);
        s.numeric.sensitiveTo = kApproxMath;
        s.numeric.compute     = TypeCode::Void;
        attempt(os, "bounded_without_type", "semtest:bounded", s);
    }
    {
        OperatorSemantics s   = valid("semtest:exact");
        s.numeric.cls         = NumericClass::Exact;
        s.numeric.sensitiveTo = kReassociate;
        attempt(os, "exact_with_rounding_permission", "semtest:exact", s);
        s.numeric.sensitiveTo = kNoPermissions;
        s.numeric.ulpBound    = 1;
        attempt(os, "exact_with_ulp", "semtest:exact", s);
    }
    {
        OperatorSemantics s = valid("semtest:rounded");
        s.numeric.ulpBound  = 1;
        attempt(os, "rounded_with_ulp", "semtest:rounded", s);
        s.numeric.ulpBound = 0;
        s.numeric.compute  = TypeCode::Int64;
        attempt(os, "rounded_without_float_type", "semtest:rounded", s);
    }
    {
        OperatorSemantics s   = valid("semtest:opaque");
        s.numeric.cls         = NumericClass::Opaque;
        s.numeric.sensitiveTo = kContract;
        attempt(os, "opaque_not_sensitive_to_all", "semtest:opaque", s);
    }

    // ---- Trait derivation (table-driven): the traits are the record's facts, and a trait
    // declared with set() before the semantics is not lost when they are declared, nor is a
    // semantic fact lost when a trait is changed afterwards. Every line below would change if
    // a trait were mapped from somewhere else than sem::OperatorSemantics (traitsOf).
    auto printTraits = [&](std::string_view uri) {
        const auto t = table.find(uri);
        if (!t) {
            os << "SEMANTICS traits " << uri << " undeclared\n";
            return;
        }
        os << "SEMANTICS traits " << uri << " pure=" << t->pure
           << " elementwise=" << t->elementwise << " stateOnly=" << t->stateOnly
           << " mayFault=" << t->mayFault << " callsGraphs=" << t->callsGraphs
           << " readsGraphs=" << t->readsGraphs << "\n";
    };
    table.set("semtest:derived_pure", {.pure = true, .mayFault = false});
    printTraits("semtest:derived_pure");
    // set() after the semantics: the same record's effect changes, nothing else is lost.
    table.set("semtest:derived_pure", {.stateOnly = true, .mayFault = false});
    printTraits("semtest:derived_pure");
    table.set("semtest:derived_calls", {.pure = false, .mayFault = true});
    {
        // Declaring element calls is declaring that the operator calls graphs: callsGraphs is
        // derived from the fact, so no separate trait bit has to agree with it.
        OperatorSemantics s = valid("semtest:derived_calls");
        s.elementCalls      = ElementCalls{.shape = ElementShape::Map, .independent = true};
        attempt(os, "derived_calls", "semtest:derived_calls", std::move(s));
    }
    printTraits("semtest:derived_calls");
    table.set("semtest:derived_elementwise", {.pure = true});
    {
        OperatorSemantics s = valid("semtest:derived_elementwise");
        s.binary            = BinaryAlgebra{.laws = {}, .elementwise = true};
        attempt(os, "derived_elementwise", "semtest:derived_elementwise", std::move(s));
    }
    printTraits("semtest:derived_elementwise");
    // The trait still follows the fault list when the fault list is the fact.
    table.set("semtest:derived_faults", {.pure = true, .mayFault = true});
    {
        OperatorSemantics s = valid("semtest:derived_faults");
        s.faults            = std::vector<FaultSpec>{{FaultKind::DivisionByZero, 1}};
        attempt(os, "derived_faults", "semtest:derived_faults", std::move(s));
    }
    printTraits("semtest:derived_faults");
    // A record with semantics but no effect fact: nobody declared the traits.
    attempt(
        os,
        "derived_undeclared",
        "semtest:derived_undeclared",
        valid("semtest:derived_undeclared"));
    printTraits("semtest:derived_undeclared");

    // ---- Queries. ----
    const auto id = table.lookup("semtest:ok");
    os << "SEMANTICS key " << (id && table.key(*id) ? table.key(*id)->toString() : "none") << "\n";
    os << "SEMANTICS undeclared_key "
       << (table.key(table.intern("semtest:never")) ? "present" : "none") << "\n";
    auto bindText = [&](std::string_view text) {
        const auto key = OperatorKey::parse(text);
        if (!key) {
            return std::string("unparsable");
        }
        const auto bound = table.bind(*key);
        return bound ? (bound == id ? std::string("bound") : std::string("other")) : "refused";
    };
    // The declared version is 1.2: a recorded 1.0, 1.1 or 1.2 binds, 1.3 and 2.0 do not.
    for (const char *text :
         {"semtest:ok@1.0", "semtest:ok@1.2", "semtest:ok@1.3", "semtest:ok@2.0",
          "semtest:missing@1.0", "semtest:never@1.0", "semtest:ok@1", "semtest:ok@1.x"}) {
        os << "SEMANTICS bind " << text << " " << bindText(text) << "\n";
    }
    const uint64_t before = table.declarationEpoch();
    attempt(os, "replace_higher", "semtest:ok", valid("semtest:ok", 1, 3));
    os << "SEMANTICS epoch_increments " << (table.declarationEpoch() > before ? "yes" : "no")
       << "\n";
    const uint64_t afterAccepted = table.declarationEpoch();
    attempt(os, "rejected_keeps_epoch", "semtest:ok", valid("semtest:ok", 1, 0));
    os << "SEMANTICS epoch_unchanged_on_reject "
       << (table.declarationEpoch() == afterAccepted ? "yes" : "no") << "\n";
    os << "SEMANTICS builtin_key " << OperatorKey::of(":op/add_d").toString() << " protocol='"
       << camel::core::symbolText(OperatorKey::of(":op/add_d").protocol) << "'\n";

    // ---- Permission names. ----
    for (const char *name : {"reassociate", "contract", "approx_math", "change_precision",
                             "finite_math", "no_nans", "flush_subnormals", "fast"}) {
        const auto mask = parsePermission(name);
        os << "SEMANTICS permission " << name << " "
           << (mask ? "[" + permissionsText(*mask) + "]" : std::string("unknown")) << "\n";
    }
    os << "SEMANTICS permissions_text ["
       << permissionsText(kReassociate | kContract | kNoNans | kNoInfs) << "]\n";
    return program;
}
