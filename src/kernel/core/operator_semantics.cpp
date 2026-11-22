// Copyright (c) the OpenCML Organization
// SPDX-License-Identifier: MIT

/*
 * The value types of the operator semantics record (see operator_semantics.h): the text forms
 * of keys, permissions, numeric classes and fault kinds. The declarations themselves live in
 * the operator table (operator_table.cpp).
 */

#include "camel/core/operator_semantics.h"

#include "camel/core/error/diagnostics.h"

#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <system_error>
#include <utility>

namespace camel::core::sem {

namespace {

/// The source names of the ten permission bits, in bit order.
constexpr std::array<std::pair<Permission, std::string_view>, 10> kPermissionNames = {{
    {kReassociate, "reassociate"},
    {kContract, "contract"},
    {kApproxMath, "approx_math"},
    {kChangePrecision, "change_precision"},
    {kNoNans, "no_nans"},
    {kNoInfs, "no_infs"},
    {kNoSignedZeros, "no_signed_zeros"},
    {kFlushSubnormals, "flush_subnormals"},
    {kNonDeterministic, "nondeterministic"},
    {kRefineAccuracy, "refine_accuracy"},
}};

/// Parses a decimal uint16_t that is the whole of `text`.
std::optional<uint16_t> parseVersionPart(std::string_view text) {
    uint16_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || ec != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

} // namespace

// ---- OperatorKey ---------------------------------------------------------------------------

std::string OperatorKey::uri() const {
    return std::format("{}:{}", symbolText(protocol), symbolText(name));
}

std::string OperatorKey::toString() const { return std::format("{}@{}.{}", uri(), major, minor); }

OperatorKey OperatorKey::of(std::string_view uri, uint16_t major, uint16_t minor) {
    // The protocol is everything before the first ':'; a URI without one has an empty
    // protocol and is all name.
    const size_t colon     = uri.find(':');
    const std::string_view protocol = colon == std::string_view::npos ? "" : uri.substr(0, colon);
    const std::string_view name     = colon == std::string_view::npos ? uri : uri.substr(colon + 1);
    return OperatorKey{internSymbol(protocol), internSymbol(name), major, minor};
}

std::optional<OperatorKey> OperatorKey::parse(std::string_view text) {
    const size_t at = text.rfind('@');
    if (at == std::string_view::npos || text.find(':') == std::string_view::npos ||
        text.find(':') > at) {
        return std::nullopt;
    }
    const std::string_view version = text.substr(at + 1);
    const size_t dot               = version.find('.');
    if (dot == std::string_view::npos) {
        return std::nullopt;
    }
    const auto major = parseVersionPart(version.substr(0, dot));
    const auto minor = parseVersionPart(version.substr(dot + 1));
    if (!major || !minor) {
        return std::nullopt;
    }
    return of(text.substr(0, at), *major, *minor);
}

// ---- Permissions ---------------------------------------------------------------------------

std::string_view permissionName(Permission permission) {
    for (const auto &[bit, name] : kPermissionNames) {
        if (bit == permission) {
            return name;
        }
    }
    return "";
}

std::optional<PermissionMask> parsePermission(std::string_view name) {
    if (name == "finite_math") {
        return kFiniteMath;
    }
    for (const auto &[bit, text] : kPermissionNames) {
        if (text == name) {
            return static_cast<PermissionMask>(bit);
        }
    }
    return std::nullopt;
}

std::string permissionsText(PermissionMask mask) {
    std::string out;
    auto add = [&](std::string_view name) {
        out += out.empty() ? "" : ", ";
        out += name;
    };
    const bool finite = (mask & kFiniteMath) == kFiniteMath;
    for (const auto &[bit, name] : kPermissionNames) {
        if ((mask & bit) == 0 || (finite && (bit & kFiniteMath) != 0)) {
            continue;
        }
        add(name);
    }
    if (finite) {
        add("finite_math");
    }
    return out;
}

// ---- The numeric contract of a rewrite -----------------------------------------------------

double elementEpsilon(type::TypeCode element) {
    switch (element) {
    case type::TypeCode::BFloat16:
        return 0x1p-8; // 2^-8
    case type::TypeCode::Float16:
        return 0x1p-10; // 2^-10
    case type::TypeCode::Float32:
        return 0x1p-24; // 2^-24
    case type::TypeCode::Float64:
        return 0x1p-53; // 2^-53
    default:
        // Not a floating element type: no number, and never f32's by default (the table exists
        // so that a reduced-precision question cannot be answered with f32's precision).
        return 0.0;
    }
}

double stepUlps(const NumericSemantics &numeric) {
    switch (numeric.cls) {
    case NumericClass::Exact:
        return 0.0;
    case NumericClass::Bounded:
        // The declaration promises `ulpBound`; a bound below one ulp is a rounding step at least.
        return numeric.ulpBound >= 1.0f ? static_cast<double>(numeric.ulpBound) : 1.0;
    case NumericClass::CorrectlyRounded:
    case NumericClass::Opaque:
        break;
    }
    // An undeclared operator is read conservatively: one rounding step.
    return 1.0;
}

RewriteErrorBound rewriteErrorBound(PermissionMask needs, size_t steps, double declaredUlps) {
    // `refine_accuracy` is not a rounding bound: the check of such a rule compares the two
    // sides' errors against a higher-precision reference, sample by sample (rewrite 8.10,
    // modules/rewrite/module.cpp). Its `declaredUlps` is still reported below, so a caller that
    // only asks for a syntactic bound sees the operators' declarations.
    if (needs == kNoPermissions) {
        // Bit for bit: the check compares the bits (a NaN is a NaN).
        return {};
    }
    RewriteErrorBound bound;
    bound.ulps = declaredUlps;
    const double stepCount = static_cast<double>(steps) + 1.0;
    if ((needs & kReassociate) != 0) {
        // The regrouped roundings may each land one ulp away from the written ones.
        bound.ulps += stepCount;
    }
    if ((needs & kContract) != 0) {
        // A contraction removes a rounding; it never adds error beyond a step's declaration.
    }
    if ((needs & kChangePrecision) != 0) {
        // Design principles 7.3: bf16 is the least precision the permission admits (8 mantissa
        // bits), and each of the rule's steps may round there.
        bound.relative += std::ldexp(1.0, -8) * stepCount;
    }
    if ((needs & kNonDeterministic) != 0) {
        // Every schedule is allowed, so there is no number to compare against.
        bound.checkable = false;
    }
    return bound;
}

// ---- Numeric classes and faults ------------------------------------------------------------

std::string_view numericClassName(NumericClass cls) {
    switch (cls) {
    case NumericClass::Exact:
        return "exact";
    case NumericClass::CorrectlyRounded:
        return "correctly_rounded";
    case NumericClass::Bounded:
        return "bounded";
    case NumericClass::Opaque:
        break;
    }
    return "opaque";
}

// ---- The effect, derived from the access list ----------------------------------------------

std::string_view effectName(Effect effect) {
    // The four answers, spelled once: `camel inspect --ops` prints this word as its "purity"
    // column, and the reference checker compares a declaration's `@pure`/`@effect` with it. There
    // is no second spelling of the effect anywhere (operator_record.h, `Record::effect`).
    switch (effect) {
    case Effect::Pure:
        return "pure";
    case Effect::StateOnly:
        return "state";
    case Effect::Effectful:
        return "effectful";
    case Effect::Undeclared:
        break;
    }
    return "undeclared";
}

bool OperatorSemantics::external() const {
    // Nobody declared its effect: the conservative reading, which is an effect outside the
    // program (design principles 3.3).
    if (declaredEffect == Effect::Undeclared) {
        return true;
    }
    if (declaredEffect == Effect::Effectful) {
        return true;
    }
    if (!resources) {
        return false;
    }
    if (resources->externalEffect) {
        return true;
    }
    if (resources->functionOperand) {
        // The accesses are a function operand's, so this answer cannot be narrower than "may
        // reach outside the program" (design principles 3.3): the reader that has the value reads
        // that value's type (`Builder::reachOfOper`, `EffectWalk::ofNodes`), which is where a
        // `map` over a pure function is known to touch nothing.
        return true;
    }
    for (const ResourceAccess &access : resources->accesses) {
        if (reachesOutside(access.resource.cls)) {
            return true;
        }
    }
    return false;
}

bool OperatorSemantics::writesForTheRegion() const {
    // The narrower half of external(): a write, not a read. An operator nobody declared is read as
    // writing the "global" resource, and so is one whose declaration says an effect exists without
    // itemizing it (design principles 3.3) -- the same conservative reading `accessList` publishes.
    // A declaration that **does** itemize its accesses is decided by them, and that is what keeps a
    // read of `env` or `console` exactly as demand-driven as it was: `get_env` is effectful and
    // reaches outside, and it writes nothing.
    //
    // A write of a **handle** is one of them too (design principles 3.1: a write to an external
    // resource or a handle is used by the region it is written in). A reference is a box the
    // program owns, so writing it
    // reaches nothing outside the program and `reachesOutside` answers false -- yet a running region
    // owes the write no less, because the reader of a box may stand somewhere this graph cannot see:
    // another graph (a callee, or the caller a box escaped to through `return`), a value a closure
    // carried away and reads on a later call, or a second name of the same handle. "No reader in
    // this graph" therefore proves nothing about whether there is a reader, and the write runs with
    // the region it is written in -- the same root a `println` has (`camel/runtime/region_roots.h`).
    // The cost is stated where the root is: a write that truly has no reader anywhere now runs once
    // instead of being deleted (unobservable; it moves debugger line coverage and timing).
    //
    // `Variable` stays out of this, and so does `Object` (assigning to a
    // `var` is not an effect -- what it writes is a value passed in and handed back). The two
    // questions this file answers are asked by different readers and are deliberately not the same:
    // "does anything outside the program see it" (`external()`, the effect summary) and "does the
    // region that runs it owe its running" (the predicate below).
    //
    // The answer itself is `sem::writesForTheRegion` (resource_semantics.cpp): a declaration states
    // its accesses in the same signature a measured body does (`signatureOf`), so there is one
    // test for "is this a write the region owes" and this method only states the spec as that
    // signature. `withCount` is 0 because the test reads the class and the mode of a use and never
    // its parameter index.
    if (declaredEffect == Effect::Undeclared) {
        return true;
    }
    if (!resources) {
        return declaredEffect == Effect::Effectful;
    }
    return camel::core::sem::writesForTheRegion(signatureOf(*resources, 0));
}

Effect OperatorSemantics::effect() const {
    // The access list decides, where it is declared: an empty one is a pure operator, and one
    // that reaches outside the program is effectful (docs/design/op-semantics/README.cn.md 4.2).
    // `Effectful` stated without an itemized access list stays effectful: the declaration says
    // the effect exists and the list is what has not been written yet.
    if (resources) {
        if (!external() && resources->none()) {
            return Effect::Pure;
        }
        return external() ? Effect::Effectful : Effect::StateOnly;
    }
    return declaredEffect;
}

std::vector<ResourceAccess> OperatorSemantics::accessList() const {
    if (resources) {
        return resources->accesses;
    }
    // No list. An operator of the built-in module must itemize its accesses (design principles
    // 3.3), and the load refuses one that does not (builtin.cpp,
    // `requireDeclaredBuiltinAccesses`), so reaching here with a built-in URI is a registration
    // mistake and not a program's error. One write of the global resource would not be a
    // declaration (`checkResources` refuses an unknown class, so no module could write it) and the
    // graph builder reads the same operator another way -- every operand read-modify-written -- so
    // one call would be on the sync chain for one reader and not for the other. The report names
    // the operator and the one change that settles it.
    if (key.uri().starts_with(':')) {
        throw camel::core::error::DiagnosticBuilder::of(
                  camel::core::error::SemanticDiag::OperatorAccessesUndeclared)
            .commit(key.uri());
    }
    // A module's operator that itemizes nothing keeps the conservative reading of design
    // principles 3.3: third-party modules are told, not refused (the same rule purity has).
    return {globalAccess(AccessMode::Write)};
}

std::string_view faultKindName(FaultKind kind) {
    switch (kind) {
    case FaultKind::DivisionByZero:
        return "division_by_zero";
    case FaultKind::IndexOutOfRange:
        return "index_out_of_range";
    case FaultKind::EmptyReduction:
        return "empty_reduction";
    case FaultKind::ConversionRange:
        return "conversion_range";
    case FaultKind::ShapeMismatch:
        return "shape_mismatch";
    case FaultKind::Other:
        break;
    }
    return "other";
}

} // namespace camel::core::sem
